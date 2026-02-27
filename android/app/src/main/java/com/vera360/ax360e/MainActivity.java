package com.vera360.ax360e;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

import org.json.JSONArray;
import org.json.JSONObject;

/**
 * ax360e — Main launcher.
 * Professional-looking dark launcher with recent games list,
 * hardware info, and quick launch.
 */
public class MainActivity extends AppCompatActivity {

    private static final String TAG = "ax360e";
    private static final String PREFS_NAME = "ax360e_prefs";
    private static final String KEY_RECENT_GAMES = "recent_games";
    private static final int MAX_RECENT = 10;

    private TextView tvGpuInfo;
    private TextView tvStatus;
    private LinearLayout recentGamesContainer;
    private Button btnOpenFile;
    private boolean nativeInitDone = false;

    private final ActivityResultLauncher<Intent> filePickerLauncher =
            registerForActivityResult(
                new ActivityResultContracts.StartActivityForResult(),
                result -> {
                    if (result.getResultCode() == Activity.RESULT_OK
                            && result.getData() != null) {
                        Uri uri = result.getData().getData();
                        if (uri != null) {
                            // Take persistable permission so we can re-open later
                            try {
                                getContentResolver().takePersistableUriPermission(
                                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                            } catch (SecurityException ignored) {}
                            addRecentGame(uri);
                            launchEmulation(uri);
                        }
                    }
                }
            );

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        tvGpuInfo             = findViewById(R.id.tv_gpu_info);
        tvStatus              = findViewById(R.id.tv_status);
        btnOpenFile           = findViewById(R.id.btn_open_file);
        recentGamesContainer  = findViewById(R.id.recent_games_container);
        Button btnSettings    = findViewById(R.id.btn_settings);

        // ── Vulkan check ────────────────────────────────────────────────
        boolean vulkanOk = false;
        String gpuName = "Unknown";
        try {
            vulkanOk = NativeBridge.isVulkanAvailable();
            if (vulkanOk) gpuName = NativeBridge.getGpuName();
        } catch (Throwable t) {
            tvStatus.setText("Native library failed to load");
            btnOpenFile.setEnabled(false);
            return;
        }

        if (!vulkanOk) {
            tvGpuInfo.setText("Vulkan not available");
            tvStatus.setText("This device is not supported");
            btnOpenFile.setEnabled(false);
            return;
        }

        tvGpuInfo.setText(gpuName);
        tvStatus.setText("Ready");

        // ── Init native core (no surface yet) ───────────────────────────
        String dataDir = getFilesDir().getAbsolutePath();
        NativeBridge.init(dataDir);
        nativeInitDone = true;

        btnOpenFile.setOnClickListener(v -> openFilePicker());
        btnSettings.setOnClickListener(v -> openSettings());

        loadRecentGames();
    }

    // ── File picker ─────────────────────────────────────────────────────
    private void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        filePickerLauncher.launch(intent);
    }

    // ── Launch emulation ────────────────────────────────────────────────
    private void launchEmulation(Uri gameUri) {
        Intent intent = new Intent(this, EmulationActivity.class);
        intent.setData(gameUri);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivity(intent);
    }

    // ── Recent games ────────────────────────────────────────────────────
    private void addRecentGame(Uri uri) {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        try {
            JSONArray arr = new JSONArray(prefs.getString(KEY_RECENT_GAMES, "[]"));
            // Remove duplicate
            for (int i = arr.length() - 1; i >= 0; i--) {
                JSONObject obj = arr.getJSONObject(i);
                if (uri.toString().equals(obj.getString("uri"))) {
                    arr.remove(i);
                }
            }
            // Add to front
            JSONObject entry = new JSONObject();
            entry.put("uri", uri.toString());
            entry.put("name", getDisplayName(uri));
            entry.put("time", System.currentTimeMillis());
            JSONArray newArr = new JSONArray();
            newArr.put(entry);
            for (int i = 0; i < Math.min(arr.length(), MAX_RECENT - 1); i++) {
                newArr.put(arr.get(i));
            }
            prefs.edit().putString(KEY_RECENT_GAMES, newArr.toString()).apply();
        } catch (Exception ignored) {}
    }

    private void loadRecentGames() {
        recentGamesContainer.removeAllViews();
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        try {
            JSONArray arr = new JSONArray(prefs.getString(KEY_RECENT_GAMES, "[]"));
            if (arr.length() == 0) {
                TextView empty = new TextView(this);
                empty.setText("No recent games — tap Open Game to begin");
                empty.setTextColor(0x88FFFFFF);
                empty.setTextSize(14f);
                empty.setPadding(0, 16, 0, 0);
                recentGamesContainer.addView(empty);
                return;
            }
            for (int i = 0; i < arr.length(); i++) {
                JSONObject obj = arr.getJSONObject(i);
                String name = obj.optString("name", "Unknown");
                String uriStr = obj.getString("uri");

                TextView tv = new TextView(this);
                tv.setText("▸  " + name);
                tv.setTextColor(0xDDFFFFFF);
                tv.setTextSize(16f);
                tv.setPadding(0, 24, 0, 24);
                tv.setBackgroundResource(android.R.drawable.list_selector_background);
                tv.setClickable(true);
                tv.setFocusable(true);
                tv.setOnClickListener(v -> {
                    Uri uri = Uri.parse(uriStr);
                    addRecentGame(uri);
                    launchEmulation(uri);
                });
                recentGamesContainer.addView(tv);
            }
        } catch (Exception ignored) {}
    }

    private String getDisplayName(Uri uri) {
        String path = uri.getLastPathSegment();
        if (path != null) {
            int slash = path.lastIndexOf('/');
            if (slash >= 0) path = path.substring(slash + 1);
            int colon = path.lastIndexOf(':');
            if (colon >= 0) path = path.substring(colon + 1);
            return path;
        }
        return uri.toString();
    }

    // ── Settings ────────────────────────────────────────────────────────
    private void openSettings() {
        new AlertDialog.Builder(this, R.style.Theme_Vera360_Dialog)
            .setTitle("Settings")
            .setItems(new String[]{
                "Graphics: Vulkan 1.1",
                "Resolution: Native",
                "Frame limit: 30 FPS",
                "Touch overlay opacity: 80%",
                "About Vera360"
            }, (dialog, which) -> {
                if (which == 4) {
                    new AlertDialog.Builder(this, R.style.Theme_Vera360_Dialog)
                        .setTitle("Vera360 v0.2.0-edge")
                        .setMessage("Xbox 360 emulator for Android\n"
                            + "ARM64 / Vulkan 1.1\n\n"
                            + "GPU: " + tvGpuInfo.getText() + "\n"
                            + "Based on Xenia research")
                        .setPositiveButton("OK", null)
                        .show();
                } else {
                    Toast.makeText(this, "Coming soon", Toast.LENGTH_SHORT).show();
                }
            })
            .setNegativeButton("Close", null)
            .show();
    }

    @Override
    protected void onResume() {
        super.onResume();
        loadRecentGames();
    }
}
