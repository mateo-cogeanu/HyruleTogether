package info.cemu.cemu

import android.app.Activity
import android.content.Context
import android.widget.Toast
import info.cemu.cemu.common.android.context.internalFolder

object HyruleSession {
    @JvmStatic external fun start(data: String, host: String, port: Int, player: String, password: String, modelType: String = "0", modelData: String = "Jugador1ModelNameLongForASpecificReason:Link"): Boolean
    @JvmStatic external fun status(): String
    @JvmStatic external fun isBotw(path: String): Boolean

    var sessionPassword = ""

    fun prepare(activity: Activity, gamePath: String): Boolean {
        val prefs = activity.getSharedPreferences("hyrule", Context.MODE_PRIVATE)
        if (!prefs.getBoolean("enabled", false)) {
            val rules = activity.internalFolder().resolve("graphicPacks/BreathOfTheWild_UKMM/rules.txt")
            if (rules.isFile) {
                val path = rules.readLines().firstOrNull { it.trim().startsWith("path =") }?.substringAfter('=')?.trim()?.trim('"')
                val packs = info.cemu.cemu.nativeinterface.NativeGraphicPacks
                packs.refreshGraphicPacks()
                packs.getGraphicPackBasicInfos().filter { it.virtualPath == path }.forEach { packs.setGraphicPackActive(it.id, false) }
            }
            return true
        }
        if (!isBotw(gamePath)) {
            Toast.makeText(activity, "Multiplayer requires a recognized BOTW base game with the v208 update installed.", Toast.LENGTH_LONG).show()
            return false
        }
        val root = activity.internalFolder()
        val pack = root.resolve("graphicPacks/BreathOfTheWild_UKMM")
        val extended = root.resolve("graphicPacks/downloadedGraphicPacks/BreathOfTheWild/Mods/ExtendedMemory/rules.txt")
        if (!pack.resolve("rules.txt").isFile || !pack.resolve("content/Pack/TitleBG.pack").isFile || !extended.isFile) {
            Toast.makeText(activity, "Install the prepared multiplayer and Extended Memory packs first.", Toast.LENGTH_LONG).show()
            return false
        }
        val nativePacks = info.cemu.cemu.nativeinterface.NativeGraphicPacks
        nativePacks.refreshGraphicPacks()
        for (rules in listOf(pack.resolve("rules.txt"), extended)) {
            val virtualPath = rules.readLines().firstOrNull { it.trim().startsWith("path =") }
                ?.substringAfter('=')?.trim()?.trim('"')
            val entry = nativePacks.getGraphicPackBasicInfos().firstOrNull { it.virtualPath == virtualPath }
            if (entry == null) {
                Toast.makeText(activity, "Cemu could not load required pack: ${rules.parentFile?.name}", Toast.LENGTH_LONG).show()
                return false
            }
            nativePacks.setGraphicPackActive(entry.id, true)
        }
        val data = activity.filesDir.resolve("hyrule").apply { mkdirs() }
        for (name in listOf("ArmorMapping.txt", "WeaponDamages.txt", "QuestFlags.txt", "QuestFlagsNames.txt")) {
            activity.assets.open("hyrule/$name").use { input -> data.resolve(name).outputStream().use { input.copyTo(it) } }
        }
        val selected = prefs.getString("model", "Link") ?: "Link"
        val npcList = org.json.JSONArray(activity.assets.open("launcher/NpcData.json").bufferedReader().use { it.readText().removePrefix("\uFEFF") })
        val npc = (0 until npcList.length()).map { npcList.getJSONObject(it) }.firstOrNull { it.getString("Folder").removePrefix("Npc_") == selected }
        val modelType = if (selected == "Link") "0" else "1"
        val modelData = if (selected == "Link") "Jugador1ModelNameLongForASpecificReason:Link" else
            "${npc?.getString("Folder") ?: "Npc_$selected"}:${npc?.getString("Name") ?: selected.replace('_', ' ')}"
        val ok = start(data.absolutePath, prefs.getString("host", "") ?: "", prefs.getInt("port", 5050),
            prefs.getString("player", "Android") ?: "Android", sessionPassword, modelType, modelData)
        if (!ok) Toast.makeText(activity, status(), Toast.LENGTH_LONG).show()
        if (ok) {
            val handler = android.os.Handler(android.os.Looper.getMainLooper())
            var previous = ""
            val watch = object : Runnable {
                override fun run() {
                    if (activity.isFinishing || activity.isDestroyed) return
                    val current = status()
                    if (current != previous) {
                        Toast.makeText(activity, current, Toast.LENGTH_LONG).show()
                        previous = current
                    }
                    handler.postDelayed(this, 2000)
                }
            }
            handler.post(watch)
        }
        return ok
    }
}
