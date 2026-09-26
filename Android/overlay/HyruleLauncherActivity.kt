package info.cemu.cemu

import android.app.Application
import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.text.style.TextAlign
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import info.cemu.cemu.common.android.context.internalFolder
import info.cemu.cemu.emulation.EmulationActivity
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import info.cemu.cemu.nativeinterface.NativeGameTitles
import info.cemu.cemu.nativeinterface.NativeSettings
import kotlinx.coroutines.*
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.InetSocketAddress
import java.net.Socket

private val Cyan = Color(0xff82e3ef)
private val Panel = Color(0x99050c10)
private val Edge = Color(0xffa9cbd0)

data class HyruleServer(val name: String, val host: String, val port: Int, val description: String)

class HyruleLauncherState(app: Application) : AndroidViewModel(app) {
    val prefs = app.getSharedPreferences("hyrule", Context.MODE_PRIVATE)
    var page by mutableIntStateOf(1)
    var player by mutableStateOf(prefs.getString("player", "Android")!!)
    var model by mutableStateOf(prefs.getString("model", "Link")!!)
    var background by mutableStateOf(prefs.getString("background", "mainWindowBackground.png")!!)
    var game by mutableStateOf(prefs.getString("game", "")!!)
    var gameId by mutableStateOf(prefs.getString("gameId", "")!!)
    var selected by mutableIntStateOf(0)
    var servers by mutableStateOf(loadServers())
    var message by mutableStateOf<String?>(null)
    var busy by mutableStateOf(false)
    var progress by mutableStateOf(Triple(0L, 0L, ""))
    var pendingKind by mutableStateOf(prefs.getString("pendingKind", "game")!!)
    var serverStatus by mutableStateOf("")
    private var job: Job? = null
    val passwords = mutableMapOf<String, String>()
    private fun app() = getApplication<Application>()
    private fun loadServers(): List<HyruleServer> = try {
        val list = JSONArray(prefs.getString("servers", "[]"))
        (0 until list.length()).map { list.getJSONObject(it).let { o -> HyruleServer(o.getString("name"), o.getString("host"), o.getInt("port"), o.optString("description")) } }
    } catch (_: Exception) { emptyList() }
    fun save() {
        val list = JSONArray()
        servers.forEach { list.put(JSONObject().put("name", it.name).put("host", it.host).put("port", it.port).put("description", it.description)) }
        prefs.edit().putString("player", player.trim()).putString("model", model).putString("background", background)
            .putString("servers", list.toString()).apply()
    }
    fun chooseImport(kind: String) { pendingKind = kind; prefs.edit().putString("pendingKind", kind).apply() }
    fun cancel() { job?.cancel() }
    fun import(uri: Uri) {
        if (busy) return
        val kind = pendingKind
        busy = true
        progress = Triple(0, 0, "Checking selected folder…")
        job = viewModelScope.launch {
            try {
                val result = withContext(Dispatchers.IO) {
                    val source = HyruleDocument(app(), DocumentFile.fromTreeUri(app(), uri) ?: error("Cannot open this folder"))
                    val report: (Long, Long, String) -> Unit = { done, total, file -> progress = Triple(done, total, file) }
                    if (kind == "packs") {
                        val pack = source.child("BreathOfTheWild_UKMM") ?: error("Select a prepared graphicPacks folder containing BreathOfTheWild_UKMM.")
                        require(pack.child("rules.txt") != null && pack.child("content")?.child("Pack")?.child("TitleBG.pack") != null) { "The selected multiplayer pack is incomplete." }
                        val downloaded = source.child("downloadedGraphicPacks")
                        require(downloaded?.child("BreathOfTheWild")?.child("Mods")?.child("ExtendedMemory")?.child("rules.txt") != null) { "This folder is missing Extended Memory." }
                        val graphics = app().internalFolder().resolve("graphicPacks")
                        val extended = downloaded!!.child("BreathOfTheWild")!!.child("Mods")!!.child("ExtendedMemory")!!
                        HyruleInstaller.copyAtomically(extended.children(), graphics.resolve("downloadedGraphicPacks/BreathOfTheWild/Mods/ExtendedMemory"), report)
                        HyruleInstaller.copyAtomically(pack.children(), graphics.resolve("BreathOfTheWild_UKMM"), report)
                        "Multiplayer and Extended Memory packs installed."
                    } else {
                        if (kind != "game") require(gameId.isNotEmpty()) { "Add the base game before installing its update or DLC." }
                        val (title, folder) = HyruleInstaller.installTitle(source, kind, File(NativeActiveSettings.getMLCPath()), if (kind == "game") null else gameId, report)
                        NativeGameTitles.addTitleFromPath(folder.absolutePath)
                        NativeGameTitles.refreshCafeTitleList()
                        if (kind == "game") {
                            game = folder.resolve("code/U-King.rpx").absolutePath
                            gameId = title.id
                            prefs.edit().putString("game", game).putString("gameId", gameId).apply()
                        }
                        "BOTW ${if (kind == "game") "base game" else kind} installed • ${title.region} • v${title.version}"
                    }
                }
                message = result
            } catch (_: CancellationException) { message = "Installation cancelled. The previous installation was kept." }
            catch (e: Exception) { message = "Installation failed: ${e.message}" }
            finally { busy = false }
        }
    }
    fun setupIssues(): List<String> {
        val issues = mutableListOf<String>()
        if (game.isEmpty() || !File(game).isFile) issues += "Add your BOTW base game in Settings."
        val suffix = gameId.takeLast(8)
        val update = File(NativeActiveSettings.getMLCPath(), "usr/title/0005000e/$suffix/meta/meta.xml")
        if (!update.isFile || runCatching { update.inputStream().use { HyruleInstaller.metadata(it, "update", gameId) } }.isFailure)
            issues += "Install the matching BOTW v208 update."
        val graphics = app().internalFolder().resolve("graphicPacks")
        if (!graphics.resolve("BreathOfTheWild_UKMM/content/Pack/TitleBG.pack").isFile ||
            !graphics.resolve("downloadedGraphicPacks/BreathOfTheWild/Mods/ExtendedMemory/rules.txt").isFile)
            issues += "Import your prepared multiplayer graphic packs in Settings."
        if (!validField(player, 32)) issues += "Enter a valid player name in Settings."
        return issues
    }
    fun refreshServer() {
        val server = servers.getOrNull(selected) ?: return
        serverStatus = "Checking…"
        viewModelScope.launch {
            serverStatus = withContext(Dispatchers.IO) {
                runCatching { Socket().use { it.connect(InetSocketAddress(server.host, server.port), 2000) }; "Server reachable" }
                    .getOrElse { "Server unavailable" }
            }
        }
    }
    companion object {
        fun validField(s: String, max: Int, optional: Boolean = false) = (optional || s.isNotBlank()) && s.toByteArray().size <= max && !s.startsWith("[") && s.none { it in ";\r\n" }
        fun localSource(file: File): HyruleSource = object : HyruleSource {
            override val name get() = file.name
            override val directory get() = file.isDirectory
            override val size get() = file.length()
            override fun children() = file.listFiles()?.map { localSource(it) } ?: emptyList()
            override fun open() = file.inputStream()
        }
    }
}

class HyruleLauncherActivity : AppCompatActivity() {
    private val state by viewModels<HyruleLauncherState>()
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme(primary = Cyan, surface = Color(0xff11191d), background = Color(0xff11191d))) {
                CompositionLocalProvider(LocalContentColor provides Color.White) { Launcher(state) }
            }
        }
    }
    private fun launch(server: HyruleServer, password: String) {
        state.save()
        val issues = state.setupIssues()
        if (issues.isNotEmpty()) { state.message = issues.joinToString("\n\n"); state.page = 0; return }
        state.prefs.edit().putString("host", server.host).putInt("port", server.port).putBoolean("enabled", true).apply()
        HyruleSession.sessionPassword = password
        NativeSettings.saveSettings()
        startActivity(Intent(this, EmulationActivity::class.java).setAction(Intent.ACTION_VIEW)
            .putExtra(EmulationActivity.EXTRA_LAUNCH_PATH, state.game))
    }
    @Composable private fun Artwork(path: String, modifier: Modifier = Modifier, chroma: Boolean = false, scale: ContentScale = ContentScale.Fit) {
        val bitmap = remember(path) {
            runCatching { assets.open("launcher/$path").use { BitmapFactory.decodeStream(it) }?.let { original ->
                if (!chroma) original else original.copy(android.graphics.Bitmap.Config.ARGB_8888, true).apply {
                    val pixels = IntArray(width * height); getPixels(pixels, 0, width, 0, 0, width, height)
                    pixels.indices.forEach { i -> val c = pixels[i]; val r = android.graphics.Color.red(c); val g = android.graphics.Color.green(c); val b = android.graphics.Color.blue(c)
                        if (g > 140 && g > r * 1.65 && g > b * 1.45) pixels[i] = c and 0x00ffffff }
                    setPixels(pixels, 0, width, 0, 0, width, height)
                }
            }?.asImageBitmap() }.getOrNull()
        }
        if (bitmap != null) Image(bitmap, contentDescription = null, modifier = modifier, contentScale = scale)
    }
    @Composable private fun Action(text: String, enabled: Boolean = true, modifier: Modifier = Modifier, action: () -> Unit) {
        OutlinedButton(onClick = action, enabled = enabled, modifier = modifier.heightIn(min = 48.dp),
            shape = RoundedCornerShape(3.dp), border = BorderStroke(1.dp, Edge),
            colors = ButtonDefaults.outlinedButtonColors(containerColor = Color(0x55020c10), contentColor = Color.White)) {
            Text(text, fontWeight = FontWeight.Bold, fontStyle = FontStyle.Italic)
        }
    }
    @Composable private fun Heading(text: String) { Text(text, fontSize = 24.sp, fontStyle = FontStyle.Italic, fontWeight = FontWeight.Bold, modifier = Modifier.fillMaxWidth().background(Color(0x55000000)).padding(12.dp)) }
    @Composable private fun Launcher(s: HyruleLauncherState) {
        val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri -> if (uri != null) s.import(uri) }
        val pick: (String) -> Unit = { kind -> s.chooseImport(kind); picker.launch(null) }
        var editing by remember { mutableStateOf<HyruleServer?>(null) }
        var editIndex by remember { mutableIntStateOf(-1) }
        var direct by remember { mutableStateOf(false) }
        var showServer by remember { mutableStateOf(false) }
        var passwordServer by remember { mutableStateOf<HyruleServer?>(null) }
        val names = listOf("Settings", "Lobby Browser", "Model Selection")
        Box(Modifier.fillMaxSize()) {
            Artwork(if (s.background == "mainWindowBackground.png") s.background else "Backgrounds/${s.background}", Modifier.matchParentSize(), scale = ContentScale.Crop)
            Column(Modifier.fillMaxSize().safeDrawingPadding()) {
                BoxWithConstraints {
                    val compact = maxWidth < 600.dp
                    Row(Modifier.fillMaxWidth().background(Panel).padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                        TextButton(onClick = { s.page-- }, enabled = s.page > 0 && !s.busy,
                            modifier = Modifier.width(if (compact) 48.dp else 170.dp).semantics { contentDescription = "Previous page" }) {
                            Text(if (s.page > 0) { if (compact) "◀" else "◀  ${names[s.page-1]}" } else "", color = Color.White)
                        }
                        Text(names[s.page], fontSize = (if (compact) 22 else 28).sp, fontStyle = FontStyle.Italic,
                            fontWeight = FontWeight.Bold, textAlign = TextAlign.Center, modifier = Modifier.weight(1f).padding(vertical = 12.dp))
                        TextButton(onClick = { s.page++ }, enabled = s.page < 2 && !s.busy,
                            modifier = Modifier.width(if (compact) 48.dp else 170.dp).semantics { contentDescription = "Next page" }) {
                            Text(if (s.page < 2) { if (compact) "▶" else "${names[s.page+1]}  ▶" } else "", color = Color.White)
                        }
                    }
                }
                Box(Modifier.weight(1f).padding(10.dp)) {
                    when (s.page) {
                        0 -> Settings(s, pick)
                        1 -> Lobby(s, add = { editing = null; editIndex = -1; direct = false; showServer = true },
                            direct = { editing = null; editIndex = -1; direct = true; showServer = true },
                            edit = { editIndex = s.selected; editing = s.servers[s.selected]; direct = false; showServer = true },
                            connect = { passwordServer = s.servers.getOrNull(s.selected) })
                        2 -> Models(s)
                    }
                }
                Text("Hyrule Together  •  Bundled Cemu Runtime", fontSize = 12.sp, fontStyle = FontStyle.Italic,
                    modifier = Modifier.fillMaxWidth().background(Panel).padding(12.dp))
            }
        }
        if (showServer) ServerEditor(editing, direct, close = { showServer = false }) { server, password ->
            if (!direct) {
                s.servers = s.servers.toMutableList().apply { if (editIndex >= 0) set(editIndex, server) else add(server) }
                s.selected = if (editIndex >= 0) editIndex else s.servers.lastIndex
                s.passwords["${server.host}:${server.port}"] = password
                s.save()
            }
            showServer = false
            if (direct) launch(server, password)
        }
        passwordServer?.let { server ->
            var password by remember { mutableStateOf(s.passwords["${server.host}:${server.port}"] ?: "") }
            AlertDialog(onDismissRequest = { passwordServer = null }, title = { Text("Connect to ${server.name}") }, text = {
                Column { Text("${server.host}:${server.port}")
                    OutlinedTextField(password, { password = it }, label = { Text("Password (optional)") }, visualTransformation = PasswordVisualTransformation(), singleLine = true) }
            }, confirmButton = { TextButton(onClick = {
                if (HyruleLauncherState.validField(password, 128, true)) { passwordServer = null; launch(server, password) }
                else s.message = "The password contains unsupported characters."
            }) { Text("CONNECT") } }, dismissButton = { TextButton(onClick = { passwordServer = null }) { Text("Cancel") } })
        }
        s.message?.let { message -> AlertDialog(onDismissRequest = { s.message = null }, title = { Text("Hyrule Together") },
            text = { Text(message, Modifier.verticalScroll(rememberScrollState())) }, confirmButton = { TextButton(onClick = { s.message = null }) { Text("OK") } }) }
        if (s.busy) AlertDialog(onDismissRequest = {}, title = { Text("Installing ${s.pendingKind}") }, text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                val (done, total, name) = s.progress
                if (total > 0) LinearProgressIndicator(progress = { (done.toFloat() / total).coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth()) else LinearProgressIndicator(Modifier.fillMaxWidth())
                Text(if (total > 0) "${done / 1048576} / ${total / 1048576} MB" else "Checking files…")
                Text(name, maxLines = 3)
            }
        }, confirmButton = { TextButton(onClick = s::cancel) { Text("Cancel installation") } })
    }
    @Composable private fun Settings(s: HyruleLauncherState, pick: (String) -> Unit) {
        BoxWithConstraints {
            val wide = maxWidth > 650.dp
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                if (wide) Column(Modifier.weight(0.8f).fillMaxHeight().background(Panel).border(1.dp, Edge), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
                    Artwork("ColoredLogo.png", Modifier.fillMaxWidth().height(270.dp))
                    Text("CROSS-PLATFORM EDITION", color = Cyan, fontStyle = FontStyle.Italic)
                }
                Column(Modifier.weight(1f).fillMaxHeight().background(Panel).border(1.dp, Edge).verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text("PLAYER NAME", fontSize = 12.sp, fontWeight = FontWeight.Bold)
                    OutlinedTextField(s.player, { s.player = it }, singleLine = true, modifier = Modifier.fillMaxWidth())
                    Text("BREATH OF THE WILD — U-KING.RPX", fontSize = 12.sp, fontWeight = FontWeight.Bold)
                    Text(if (s.game.isEmpty()) "No game added" else "BOTW • ${when(s.gameId.takeLast(8)) { "101c9300" -> "Japan"; "101c9400" -> "USA"; else -> "Europe" }}\n${s.game}", fontSize = 13.sp)
                    Action(if (s.game.isEmpty()) "BROWSE — ADD BOTW" else "BROWSE — REPLACE BOTW", action = { pick("game") })
                    Text("Choose your decrypted game folder containing code, content, and meta. Files are copied into Hyrule Together.", fontSize = 12.sp, color = Edge)
                    Action("INSTALL BOTW UPDATE", s.game.isNotEmpty(), action = { pick("update") })
                    Action("INSTALL BOTW DLC", s.game.isNotEmpty(), action = { pick("dlc") })
                    Action("IMPORT PREPARED MULTIPLAYER PACKS", action = { pick("packs") })
                    Action("MANAGE CEMU GRAPHIC PACKS", action = { advanced("packs") })
                    Action("CONTROLLER & EMULATOR SETTINGS", action = { advanced("settings") })
                    Text("BACKGROUND", fontSize = 12.sp, fontWeight = FontWeight.Bold)
                    var backgrounds by remember { mutableStateOf(false) }
                    Box { Action(s.background.substringBeforeLast('.'), action = { backgrounds = true })
                        DropdownMenu(backgrounds, { backgrounds = false }) {
                            (listOf("mainWindowBackground.png") + (assets.list("launcher/Backgrounds")?.toList() ?: emptyList())).forEach { name ->
                                DropdownMenuItem(text = { Text(name.substringBeforeLast('.')) }, onClick = { s.background = name; s.save(); backgrounds = false })
                            }
                        }
                    }
                    Action("◆  SAVE SETTINGS  ◆", action = { if (!HyruleLauncherState.validField(s.player, 32)) s.message = "Enter a player name of up to 32 UTF-8 bytes without semicolons or a leading [." else { s.save(); s.message = "Settings saved." } })
                    Action("◆  RUN SETUP CHECK  ◆", action = { s.save(); s.message = s.setupIssues().joinToString("\n\n").ifEmpty { "Game, v208 update, multiplayer packs, and player settings are ready." } })
                }
            }
        }
    }
    private fun advanced(page: String) { startActivity(Intent(this, MainActivity::class.java).putExtra("hyrule_page", page)) }
    @Composable private fun Lobby(s: HyruleLauncherState, add: () -> Unit, direct: () -> Unit, edit: () -> Unit, connect: () -> Unit) {
        BoxWithConstraints {
            val wide = maxWidth > 650.dp
            val list: @Composable () -> Unit = {
                Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) { Action("Add Server", modifier = Modifier.weight(1f), action = add); Action("Direct IP", modifier = Modifier.weight(1f), action = direct) }
                    Action("Refresh", s.servers.isNotEmpty(), action = s::refreshServer)
                    Column(Modifier.weight(1f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        if (s.servers.isEmpty()) Text("No servers yet\nChoose Add Server or Direct IP", fontSize = 18.sp, fontStyle = FontStyle.Italic, modifier = Modifier.fillMaxWidth().background(Panel).padding(24.dp))
                        s.servers.forEachIndexed { index, server ->
                            Column(Modifier.fillMaxWidth().background(if (s.selected == index) Color(0x99316c78) else Panel).border(if (s.selected == index) 2.dp else 1.dp, Edge).clickable { s.selected = index; s.serverStatus = "" }.padding(16.dp)) {
                                Text(server.name, fontSize = 21.sp, fontWeight = FontWeight.Bold, fontStyle = FontStyle.Italic)
                                Text("${server.host}:${server.port}  •  Normal", fontSize = 13.sp)
                            }
                        }
                    }
                }
            }
            val detail: @Composable () -> Unit = {
                val server = s.servers.getOrNull(s.selected)
                Column(Modifier.fillMaxSize().background(Panel).border(1.dp, Edge).padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Heading(server?.name ?: "Select a server")
                    Text(server?.let { "${it.host}:${it.port}  •  Gamemode: Normal" } ?: "", color = Cyan)
                    Text(s.serverStatus, color = Cyan)
                    Text(server?.description?.ifBlank { "A Breath of the Wild multiplayer server." } ?: "Add a server to begin your adventure.", fontSize = 18.sp, fontStyle = FontStyle.Italic, modifier = Modifier.weight(1f).verticalScroll(rememberScrollState()))
                    if (server != null) Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Action("Edit", action = edit)
                        Action("Remove", action = { s.servers = s.servers.filterIndexed { i, _ -> i != s.selected }; s.selected = 0; s.save() })
                    }
                    Action("◆  CONNECT  ◆", server != null, Modifier.fillMaxWidth(), connect)
                }
            }
            if (wide) Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) { Box(Modifier.weight(1f)) { list() }; Box(Modifier.weight(1f)) { detail() } }
            else Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(10.dp)) { Box(Modifier.height(300.dp)) { list() }; Box(Modifier.height(380.dp)) { detail() } }
        }
    }
    @Composable private fun Models(s: HyruleLauncherState) {
        val models = remember { assets.list("launcher/Bust")?.filter { it.endsWith(".png") && it.substringBeforeLast('.') !in listOf("BumiiMaker", "Environmental") }?.sorted() ?: emptyList() }
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            LazyVerticalGrid(GridCells.Adaptive(100.dp), Modifier.weight(2f), verticalArrangement = Arrangement.spacedBy(8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(models) { file -> val name = file.substringBeforeLast('.')
                    Column(Modifier.background(Panel).border(if (s.model == name) 2.dp else 1.dp, if (s.model == name) Cyan else Edge).clickable { s.model = name; s.save() }.padding(6.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                        Artwork("Bust/$file", Modifier.fillMaxWidth().height(90.dp), chroma = true)
                        Text(name.replace('_', ' '), fontSize = 11.sp, maxLines = 2)
                    }
                }
            }
            Column(Modifier.weight(1f).fillMaxHeight().background(Panel).border(1.dp, Edge), horizontalAlignment = Alignment.CenterHorizontally) {
                Artwork("Body/${s.model}.png", Modifier.weight(1f).fillMaxWidth(), chroma = true)
                Text(s.model.replace('_', ' '), fontSize = 22.sp, fontStyle = FontStyle.Italic, modifier = Modifier.padding(12.dp))
                Text("Selected multiplayer model", fontSize = 12.sp, modifier = Modifier.padding(12.dp))
            }
        }
    }
    @Composable private fun ServerEditor(server: HyruleServer?, direct: Boolean, close: () -> Unit, done: (HyruleServer, String) -> Unit) {
        val keyboard = LocalSoftwareKeyboardController.current
        var name by remember { mutableStateOf(server?.name ?: if (direct) "Direct Connection" else "") }
        var host by remember { mutableStateOf(server?.host ?: "") }; var port by remember { mutableStateOf(server?.port?.toString() ?: "5050") }
        var description by remember { mutableStateOf(server?.description ?: "") }; var password by remember { mutableStateOf("") }; var error by remember { mutableStateOf("") }
        AlertDialog(onDismissRequest = close, title = { Text(if (direct) "Direct Connection" else if (server == null) "Add Server" else "Edit Server") }, text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (!direct) OutlinedTextField(name, { name = it }, label = { Text("Server name") }, singleLine = true)
                OutlinedTextField(host, { host = it }, label = { Text("Server IP or hostname") }, singleLine = true)
                OutlinedTextField(port, { port = it }, label = { Text("Port") }, singleLine = true)
                OutlinedTextField(password, { password = it }, label = { Text("Password (optional)") }, visualTransformation = PasswordVisualTransformation(), singleLine = true)
                if (!direct) OutlinedTextField(description, { description = it }, label = { Text("Description") })
                if (error.isNotEmpty()) Text(error, color = MaterialTheme.colorScheme.error)
            }
        }, confirmButton = { TextButton(onClick = {
            val p = port.toIntOrNull()
            if (name.isBlank() || !HyruleLauncherState.validField(host.trim(), 253) || p == null || p !in 1..65535 || !HyruleLauncherState.validField(password, 128, true)) error = "Enter a server name, valid address, and port from 1 to 65535."
            else { keyboard?.hide(); done(HyruleServer(name.trim(), host.trim(), p, description), password) }
        }) { Text(if (direct) "CONNECT" else "SAVE") } }, dismissButton = { TextButton(onClick = close) { Text("Cancel") } })
    }
}
