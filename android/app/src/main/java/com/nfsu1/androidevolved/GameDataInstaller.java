package com.nfsu1.androidevolved;

import android.content.Context;
import android.content.pm.PackageManager;
import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;
import android.os.Environment;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.function.IntConsumer;

/**
 * Extracts the game data bundled under assets/nfsu1 to the external storage path the engine
 * expects (gameRoot()), once per app version. The guest filesystem shim does real POSIX I/O
 * (open/read/write/mkdir) against that path, so the bundled copy has to be staged there rather
 * than read straight out of AssetManager.
 */
final class GameDataInstaller {
    private static final String ASSET_ROOT = "nfsu1";
    private static final String MARKER = ".installed";

    static File gameRoot() { return new File(Environment.getExternalStorageDirectory(), "nfsu1"); }

    static boolean needsInstall(Context context) {
        try {
            long installed = Long.parseLong(new String(
                    Files.readAllBytes(new File(gameRoot(), MARKER).toPath()), StandardCharsets.UTF_8).trim());
            return installed != versionCode(context);
        } catch (Exception error) { return true; }
    }

    /** Copies every file under assets/nfsu1 to gameRoot(), reporting progress as 0..100. */
    static void install(Context context, IntConsumer progress) throws IOException, PackageManager.NameNotFoundException {
        AssetManager assets = context.getAssets();
        List<String> files = new ArrayList<>();
        collectFiles(assets, ASSET_ROOT, files);
        long[] sizes = new long[files.size()];
        long total = 0;
        for (int i = 0; i < files.size(); i++) {
            try (AssetFileDescriptor fd = assets.openFd(files.get(i))) { sizes[i] = fd.getLength(); }
            total += sizes[i];
        }
        long copied = 0;
        byte[] buffer = new byte[1 << 20];
        for (String asset : files) {
            File target = new File(gameRoot(), asset.substring(ASSET_ROOT.length() + 1));
            target.getParentFile().mkdirs();
            try (InputStream in = assets.open(asset); FileOutputStream out = new FileOutputStream(target)) {
                int read;
                while ((read = in.read(buffer)) > 0) {
                    out.write(buffer, 0, read);
                    copied += read;
                    progress.accept((int) (copied * 100 / Math.max(total, 1)));
                }
            }
        }
        try (FileOutputStream marker = new FileOutputStream(new File(gameRoot(), MARKER))) {
            marker.write(String.valueOf(versionCode(context)).getBytes(StandardCharsets.UTF_8));
        }
    }

    private static long versionCode(Context context) throws PackageManager.NameNotFoundException {
        return context.getPackageManager().getPackageInfo(context.getPackageName(), 0).getLongVersionCode();
    }

    /** AssetManager has no concept of directories: a path is a file if listing it is empty. */
    private static void collectFiles(AssetManager assets, String path, List<String> out) throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) { out.add(path); return; }
        for (String child : children) collectFiles(assets, path + "/" + child, out);
    }

    private GameDataInstaller() {}
}
