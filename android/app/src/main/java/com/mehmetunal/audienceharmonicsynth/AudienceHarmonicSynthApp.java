package com.mehmetunal.audienceharmonicsynth;

import android.content.res.AssetManager;

import com.rmsl.juce.JuceApp;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public final class AudienceHarmonicSynthApp extends JuceApp {
    private static final String ASSET_SAMPLE_ROOT = "Samples";
    private static final String APP_SAMPLE_ROOT = "Audience Harmonic Synth/Samples";
    private static final String VERSION_MARKER = ".android-samples-1.0.1";

    @Override
    public void onCreate() {
        try {
            installBundledSamples();
        } catch (IOException ignored) {
            // JUCE will show "Samples folder not found" if extraction fails.
        }

        super.onCreate();
    }

    private void installBundledSamples() throws IOException {
        File destination = new File(getApplicationInfo().dataDir, APP_SAMPLE_ROOT);
        File marker = new File(destination, VERSION_MARKER);

        if (marker.exists()) {
            return;
        }

        if (destination.exists()) {
            deleteRecursively(destination);
        }

        if (!destination.mkdirs() && !destination.isDirectory()) {
            throw new IOException("Could not create sample directory");
        }

        copyAssetTree(getAssets(), ASSET_SAMPLE_ROOT, destination);
        marker.createNewFile();
    }

    private static void copyAssetTree(AssetManager assets, String assetPath, File destination) throws IOException {
        String[] children = assets.list(assetPath);

        if (children == null || children.length == 0) {
            copyAssetFile(assets, assetPath, destination);
            return;
        }

        if (!destination.mkdirs() && !destination.isDirectory()) {
            throw new IOException("Could not create " + destination);
        }

        for (String child : children) {
            copyAssetTree(assets, assetPath + "/" + child, new File(destination, child));
        }
    }

    private static void copyAssetFile(AssetManager assets, String assetPath, File destination) throws IOException {
        File parent = destination.getParentFile();

        if (parent != null && !parent.mkdirs() && !parent.isDirectory()) {
            throw new IOException("Could not create " + parent);
        }

        byte[] buffer = new byte[65536];

        try (InputStream input = assets.open(assetPath);
             FileOutputStream output = new FileOutputStream(destination)) {
            int n;
            while ((n = input.read(buffer)) > 0) {
                output.write(buffer, 0, n);
            }
        }
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();

        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }

        file.delete();
    }
}
