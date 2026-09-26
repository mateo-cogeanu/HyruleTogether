package info.cemu.cemu

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.text.InputType
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Toast
import info.cemu.cemu.common.android.context.internalFolder

object HyruleSession {
    @JvmStatic external fun start(data: String, host: String, port: Int, player: String, password: String): Boolean
    @JvmStatic external fun status(): String
    @JvmStatic external fun isBotw(path: String): Boolean

    fun settings(activity: Activity) {
        val prefs = activity.getSharedPreferences("hyrule", Context.MODE_PRIVATE)
        val layout = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 16, 32, 16)
        }
        val enabled = CheckBox(activity).apply { text = "Join multiplayer when launching BOTW"; isChecked = prefs.getBoolean("enabled", false) }
        layout.addView(enabled)
        fun field(hint: String, value: String) = EditText(activity).apply {
            this.hint = hint; setText(value); isSingleLine = true; layout.addView(this)
        }
        val host = field("Server address", prefs.getString("host", "") ?: "")
        val port = field("Port", prefs.getInt("port", 5050).toString()).apply { inputType = InputType.TYPE_CLASS_NUMBER }
        val player = field("Player name", prefs.getString("player", "Android") ?: "Android")
        // Password is held in memory for this app session, never persisted.
        val password = field("Server password (optional)", sessionPassword).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        val dialog = AlertDialog.Builder(activity).setTitle("Hyrule Together — connection")
            .setMessage("${status()}\nInstall BOTW v208, the prepared Hyrule Together graphic pack, and Extended Memory before joining.")
            .setView(layout).setNegativeButton("Cancel", null).setPositiveButton("Save", null).create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val h = host.text.toString().trim(); val p = port.text.toString().toIntOrNull()
                val n = player.text.toString().trim(); val secret = password.text.toString()
                if (enabled.isChecked && (h.isEmpty() || h.length > 253 || p == null || p !in 1..65535 ||
                    n.isEmpty() || n.toByteArray().size > 32 || secret.toByteArray().size > 128 ||
                    listOf(h,n,secret).any { it.startsWith("[") || it.any { ch -> ch == ';' || ch == '\n' || ch == '\r' } })) {
                    Toast.makeText(activity, "Enter a valid address, port, and player name; semicolons and a leading [ are not allowed.", Toast.LENGTH_LONG).show()
                } else {
                    prefs.edit().putBoolean("enabled", enabled.isChecked).putString("host", h)
                        .putInt("port", p ?: 5050).putString("player", n).apply()
                    sessionPassword = secret; dialog.dismiss()
                }
            }
        }
        dialog.show()
    }
    private var sessionPassword = ""

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
        val ok = start(data.absolutePath, prefs.getString("host", "") ?: "", prefs.getInt("port", 5050),
            prefs.getString("player", "Android") ?: "Android", sessionPassword)
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
