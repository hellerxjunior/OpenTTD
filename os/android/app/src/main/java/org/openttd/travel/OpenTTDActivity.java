package org.openttd.travel;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Starts OpenTTD through SDL. Before the native code runs, the game data
 * packed in the APK (base sets, languages, scripts) is copied to the app's
 * private storage (files/data/openttd), because OpenTTD reads its data from
 * normal files.
 */
public class OpenTTDActivity extends SDLActivity {
    private static final String TAG = "OpenTTD";
    private static final String ASSET_ROOT = "openttd";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        File files = getFilesDir();
        File data = new File(files, "data");
        File config = new File(files, "config");

        try {
            installData(new File(data, "openttd"));
            writeDefaultConfig(new File(config, "openttd"));
            /* The native side points HOME and XDG_* at these folders. */
        } catch (Exception e) {
            Log.e(TAG, "Preparing game data failed", e);
        }

        super.onCreate(savedInstanceState);
    }

    /** Extra command line arguments, e.g. "-d misc=3" from adb for debugging. */
    @Override
    protected String[] getArguments() {
        String args = getIntent().getStringExtra("args");
        if (args == null || args.trim().isEmpty()) return new String[0];
        return args.trim().split("\\s+");
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    /** Copy the bundled data once per installed APK version. */
    private void installData(File target) throws Exception {
        long version = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
        File stamp = new File(target, ".installed");
        if (stamp.exists() && readStamp(stamp) == version) return;

        Log.i(TAG, "Installing game data to " + target);
        int files = copyAssets(getAssets(), ASSET_ROOT, target);
        Log.i(TAG, "Installed " + files + " files");
        try (OutputStream out = new FileOutputStream(stamp)) {
            out.write(Long.toString(version).getBytes());
        }
    }

    private static long readStamp(File stamp) {
        try (InputStream in = new java.io.FileInputStream(stamp)) {
            byte[] buf = new byte[32];
            int n = in.read(buf);
            return Long.parseLong(new String(buf, 0, Math.max(n, 0)).trim());
        } catch (Exception e) {
            return -1;
        }
    }

    private static int copyAssets(AssetManager assets, String path, File target) throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            /* A file (asset directories are never empty). */
            target.getParentFile().mkdirs();
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            }
            return 1;
        }
        target.mkdirs();
        int files = 0;
        for (String child : children) {
            files += copyAssets(assets, path + "/" + child, new File(target, child));
        }
        return files;
    }

    /** Touch friendly defaults, only written before the very first start. */
    private static void writeDefaultConfig(File dir) throws IOException {
        dir.mkdirs();
        writeIfMissing(new File(dir, "openttd.cfg"),
                "[gui]\n"
                /* Drag the map with a finger (left button). */
                + "scroll_mode = 3\n");
        writeIfMissing(new File(dir, "private.cfg"),
                "[network]\n"
                /* A private game; do not ask about sending usage surveys. */
                + "participate_survey = no\n");
    }

    private static void writeIfMissing(File file, String text) throws IOException {
        if (file.exists()) return;
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes());
        }
    }
}
