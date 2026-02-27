package com.vera360.ax360e;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

/**
 * ax360e — Main launcher UI for Vera360 (Xenia Edge Android port).
 * Shows game list and settings. Launches EmulationActivity for gameplay.
 */
public class MainActivity extends AppCompatActivity {

    private static final String TAG = "ax360e";

    private TextView tvStatus;
    private Button btnOpenFile;
    private Button btnSettings;

    private final ActivityResultLauncher<Intent> filePickerLauncher =
            registerForActivityResult(
                new ActivityResultContracts.StartActivityForResult(),
                result -> {
                    if (result.getResultCode() == Activity.RESULT_OK && result.getData() != null) {
                        Uri uri = result.getData().getData();
                        if (uri != null) {
                            launchEmulation(uri);
                        }
                    }
                }
            );

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        tvStatus    = findViewById(R.id.tv_status);
        btnOpenFile = findViewById(R.id.btn_open_file);
        btnSettings = findViewById(R.id.btn_settings);

        // Check Vulkan support
        if (!NativeBridge.isVulkanAvailable()) {
            tvStatus.setText("❌ Vulkan not available on this device");
            btnOpenFile.setEnabled(false);
            return;
        }

        String gpuName = NativeBridge.getGpuName();
        tvStatus.setText("✅ Vulkan OK — " + gpuName);

        btnOpenFile.setOnClickListener(v -> openFilePicker());
        btnSettings.setOnClickListener(v -> openSettings());

        // Initialize native emulator core
        NativeBridge.init(getApplicationInfo().nativeLibraryDir);
    }

    private void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        String[] mimeTypes = {
            "application/octet-stream",    // .xex, .iso
            "application/x-iso9660-image"  // .iso
        };
        intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes);
        filePickerLauncher.launch(intent);
    }

    private void launchEmulation(Uri gameUri) {
        Intent intent = new Intent(this, EmulationActivity.class);
        intent.setData(gameUri);
        startActivity(intent);
    }

    private void openSettings() {
        Toast.makeText(this, "Settings — TODO", Toast.LENGTH_SHORT).show();
    }
}
