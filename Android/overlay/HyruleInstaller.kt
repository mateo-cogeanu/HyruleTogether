package info.cemu.cemu

import android.content.Context
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import java.io.File
import java.io.InputStream
import java.util.UUID
import org.xmlpull.v1.XmlPullParser
import android.util.Xml

/** The same importer is used by SAF and filesystem fixtures in instrumentation. */
interface HyruleSource {
    val name: String
    val directory: Boolean
    val size: Long
    fun children(): List<HyruleSource>
    fun open(): InputStream
    fun child(name: String) = children().firstOrNull { it.name == name }
}
class HyruleDocument(private val context: Context, private val file: DocumentFile) : HyruleSource {
    override val name get() = file.name ?: error("A selected file has no name")
    override val directory get() = file.isDirectory
    override val size get() = file.length()
    override fun children() = file.listFiles().map { HyruleDocument(context, it) }
    override fun open() = context.contentResolver.openInputStream(file.uri) ?: error("Cannot read $name")
}

data class HyruleTitle(val id: String, val version: Int) {
    val region get() = when (id.takeLast(8)) { "101c9300" -> "Japan"; "101c9400" -> "USA"; else -> "Europe" }
}

object HyruleInstaller {
    fun metadata(input: InputStream, kind: String, baseId: String? = null): HyruleTitle {
        val parser = Xml.newPullParser()
        parser.setFeature(XmlPullParser.FEATURE_PROCESS_NAMESPACES, true)
        parser.setInput(input, null)
        var id = ""; var version = 0
        while (parser.next() != XmlPullParser.END_DOCUMENT) {
            if (parser.eventType == XmlPullParser.START_TAG) when (parser.name) {
                "title_id" -> id = parser.nextText().trim().lowercase()
                "title_version" -> { val value = parser.nextText().trim(); version = if (value.startsWith("0x")) value.drop(2).toInt(16) else value.toInt() }
            }
        }
        require(id.matches(Regex("000500(00|0e|0c)101c(93|94|95)00"))) { "Select a Breath of the Wild title folder." }
        if (baseId != null) require(id.takeLast(8) == baseId.takeLast(8)) { "This title's region does not match your installed BOTW game." }
        when (kind) {
            "game" -> require(id.startsWith("00050000") && version == 0) { "Select the base game folder, not an update or DLC." }
            "update" -> {
                require((id.startsWith("0005000e") || id.startsWith("00050000")) && version == 208) { "Hyrule Together requires the BOTW v208 update." }
                id = "0005000e" + id.takeLast(8)
            }
            "dlc" -> require(id.startsWith("0005000c")) { "Select the BOTW DLC folder." }
            else -> error("Unknown title type")
        }
        return HyruleTitle(id, version)
    }

    suspend fun installTitle(source: HyruleSource, kind: String, mlc: File, baseId: String?, progress: (Long, Long, String) -> Unit): Pair<HyruleTitle, File> {
        for (name in listOf("code", "content", "meta")) require(source.child(name)?.directory == true) { "Select the folder containing code, content, and meta." }
        val meta = source.child("meta")?.child("meta.xml") ?: error("Missing meta/meta.xml")
        val title = meta.open().use { metadata(it, kind, baseId) }
        if (kind == "game") require(source.child("code")?.child("U-King.rpx") != null) { "The base game is missing code/U-King.rpx." }
        val target = mlc.resolve("usr/title/${title.id.take(8)}/${title.id.takeLast(8)}")
        copyAtomically(source.children().filter { it.name in listOf("code", "content", "meta") }, target, progress)
        return title to target
    }

    suspend fun copyAtomically(sources: List<HyruleSource>, target: File, progress: (Long, Long, String) -> Unit) {
        data class Entry(val source: HyruleSource, val path: String)
        val entries = mutableListOf<Entry>()
        suspend fun scan(node: HyruleSource, parent: String) {
            currentCoroutineContext().ensureActive()
            require(node.name.isNotBlank() && node.name !in listOf(".", "..") && !node.name.contains('/') && !node.name.contains('\\')) { "Invalid filename in selected folder" }
            val path = if (parent.isEmpty()) node.name else "$parent/${node.name}"
            entries += Entry(node, path)
            if (node.directory) node.children().forEach { scan(it, path) }
        }
        progress(0, 0, "Checking files…")
        sources.forEach { scan(it, "") }
        val total = entries.filter { !it.source.directory }.sumOf { it.source.size }
        require(target.parentFile!!.isDirectory || target.parentFile!!.mkdirs()) { "Cannot create installation directory" }
        require(target.parentFile!!.usableSpace > total + 64L * 1024 * 1024) { "Not enough storage. Free space and try again." }
        val staging = File(target.parentFile, ".${target.name}.install-${UUID.randomUUID()}")
        val backup = File(target.parentFile, ".${target.name}.backup-${UUID.randomUUID()}")
        var completed = 0L
        try {
            check(staging.mkdirs())
            val buffer = ByteArray(1024 * 1024)
            for ((source, path) in entries) {
                currentCoroutineContext().ensureActive()
                val out = staging.resolve(path)
                if (source.directory) { check(out.mkdirs() || out.isDirectory); continue }
                out.parentFile!!.mkdirs()
                source.open().use { input -> out.outputStream().use { output ->
                    while (true) {
                        currentCoroutineContext().ensureActive()
                        val count = input.read(buffer)
                        if (count < 0) break
                        output.write(buffer, 0, count)
                        completed += count
                        progress(completed, total, path)
                    }
                } }
                if (source.size > 0) require(out.length() == source.size) { "Incomplete copy: $path" }
            }
            currentCoroutineContext().ensureActive()
            if (target.exists()) check(target.renameTo(backup)) { "Cannot back up the previous installation" }
            if (!staging.renameTo(target)) {
                if (backup.exists()) check(backup.renameTo(target)) { "Installation failed; previous files remain at $backup" }
                error("Cannot finish installation")
            }
            backup.deleteRecursively()
        } finally { staging.deleteRecursively() }
    }
}
