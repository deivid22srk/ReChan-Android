package com.deivid22srk.rechan;

import android.app.Activity;
import android.app.Dialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Bootstrap activity: extracts ReChan support files from the APK, then makes
 * sure a disc image (SLUS-00684 .bin/.iso) is available in
 * filesDir/discimage/. If none is present the user picks a folder via SAF and
 * the image is copied into app storage. Only afterwards is GameActivity
 * (the NativeActivity) started.
 */
public class BootActivity extends Activity {

    private static final String PREFS = "rechan_boot";
    private static final String KEY_TREE_URI = "disc_folder_uri";
    private static final String KEY_COPIED_NAME = "copied_name";
    private static final String KEY_COPIED_SIZE = "copied_size";
    private static final int REQUEST_PICK_FOLDER = 41;

    private TextView statusText;
    private ProgressBar progress;
    private LinearLayout pickLayout;
    private Dialog busyDialog;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);

        statusText = new TextView(this);
        statusText.setText("Preparando dados do jogo…");
        statusText.setTextSize(16);
        statusText.setGravity(Gravity.CENTER);
        root.addView(statusText,
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT));

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setIndeterminate(true);
        LinearLayout.LayoutParams pp =
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT);
        pp.topMargin = pad / 2;
        progress.setLayoutParams(pp);
        root.addView(progress);

        pickLayout = new LinearLayout(this);
        pickLayout.setOrientation(LinearLayout.VERTICAL);
        pickLayout.setGravity(Gravity.CENTER);
        TextView hint = new TextView(this);
        hint.setText("Nenhuma imagem do jogo encontrada.\n"
                + "Escolha a pasta que contém o arquivo .bin ou .iso\n"
                + "de Jackie Chan Stuntmaster (SLUS-00684).");
        hint.setTextSize(15);
        hint.setGravity(Gravity.CENTER);
        Button choose = new Button(this);
        choose.setText("Escolher pasta");
        choose.setOnClickListener(v -> launchFolderPicker());
        pickLayout.addView(hint,
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT));
        pickLayout.addView(choose);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = pad;
        pickLayout.setLayoutParams(lp);
        pickLayout.setVisibility(View.GONE);
        root.addView(pickLayout);

        setContentView(root);

        try {
            extractSupportAssets();
            startGameIfDiscReadyOrPick();
        } catch (Exception e) {
            fail("Erro ao preparar dados: " + e.getMessage());
        }
    }

    // --- Support assets -----------------------------------------------------

    private void extractSupportAssets() throws IOException {
        List<String> paths = new ArrayList<>();
        InputStream manifestIn = getAssets().open("pc_manifest.txt");
        byte[] buf = new byte[8192];
        StringBuilder sb = new StringBuilder();
        int n;
        while ((n = manifestIn.read(buf)) > 0) {
            sb.append(new String(buf, 0, n, StandardCharsets.UTF_8));
        }
        manifestIn.close();
        for (String line : sb.toString().split("\n")) {
            line = line.trim();
            if (!line.isEmpty()) {
                paths.add(line);
            }
        }

        for (String path : paths) {
            File out = new File(getFilesDir(), path);
            if (out.isFile() && out.length() > 0) {
                continue; // already extracted
            }
            File parent = out.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                throw new IOException("mkdir failed: " + parent);
            }
            try (InputStream in = getAssets().open(path);
                 OutputStream os = new FileOutputStream(out)) {
                byte[] chunk = new byte[65536];
                int read;
                while ((read = in.read(chunk)) > 0) {
                    os.write(chunk, 0, read);
                }
            }
        }
    }

    // --- Disc image flow ----------------------------------------------------

    private void startGameIfDiscReadyOrPick() {
        File discDir = new File(getFilesDir(), "discimage");
        File[] images = discDir.listFiles((dir, name) -> {
            String lower = name.toLowerCase();
            return lower.endsWith(".bin") || lower.endsWith(".iso");
        });
        if (images != null && images.length > 0) {
            startGame();
            return;
        }

        String tree = getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_TREE_URI, null);
        if (tree != null) {
            statusText.setText("Verificando imagem do jogo…");
            DocumentInfo info = findBestImage(Uri.parse(tree));
            if (info != null) {
                copyImageAsync(Uri.parse(tree), info);
                return;
            }
        }

        showPickUi();
    }

    private void showPickUi() {
        progress.setVisibility(View.GONE);
        statusText.setText("");
        pickLayout.setVisibility(View.VISIBLE);
    }

    private void launchFolderPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_PICK_FOLDER);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICK_FOLDER || resultCode != RESULT_OK || data == null
                || data.getData() == null) {
            showPickUi();
            return;
        }
        Uri treeUri = data.getData();
        final int takeFlags = data.getFlags()
                & (Intent.FLAG_GRANT_READ_URI_PERMISSION
                        | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            getContentResolver().takePersistableUriPermission(treeUri, takeFlags);
        } catch (SecurityException ignored) {
            // Some providers don't hand out persistable grants; proceed anyway.
        }
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .putString(KEY_TREE_URI, treeUri.toString()).apply();

        statusText.setText("Procurando imagem do jogo…");
        DocumentInfo info = findBestImage(treeUri);
        if (info == null) {
            fail("A pasta escolhida não contém um arquivo .bin ou .iso.");
            return;
        }
        copyImageAsync(treeUri, info);
    }

    private static class DocumentInfo {
        String displayName;
        Uri uri;
        long size = -1;
    }

    private DocumentInfo findBestImage(Uri treeUri) {
        DocumentInfo best = null;
        try {
            Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                    treeUri, DocumentsContract.getTreeDocumentId(treeUri));
            Cursor cursor = getContentResolver().query(childrenUri,
                    new String[] {
                            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                            DocumentsContract.Document.COLUMN_SIZE,
                    }, null, null, null);
            if (cursor == null) return null;
            while (cursor.moveToNext()) {
                String name = cursor.getString(0);
                String id = cursor.getString(1);
                long size = cursor.isNull(2) ? -1 : cursor.getLong(2);
                if (name == null) continue;
                String lower = name.toLowerCase();
                if (!lower.endsWith(".bin") && !lower.endsWith(".iso")) continue;
                Uri docUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, id);
                boolean preferred = lower.contains("slus")
                        || lower.contains("jackie")
                        || lower.contains("stuntmaster");
                if (best == null || (preferred && !isPreferred(best.displayName))
                        || (preferred == isPreferred(best.displayName) && size > best.size)) {
                    DocumentInfo info = new DocumentInfo();
                    info.displayName = name;
                    info.uri = docUri;
                    info.size = size;
                    best = info;
                }
            }
            cursor.close();
        } catch (Exception e) {
            return null;
        }
        return best;
    }

    private static boolean isPreferred(String name) {
        if (name == null) return false;
        String lower = name.toLowerCase();
        return lower.contains("slus") || lower.contains("jackie")
                || lower.contains("stuntmaster");
    }

    private void copyImageAsync(final Uri treeUri, final DocumentInfo info) {
        progress.setIndeterminate(false);
        statusText.setText("Copiando " + info.displayName + " …");

        Thread worker = new Thread(() -> {
            File outFile = new File(getFilesDir(), "discimage/game.bin");
            try {
                File dir = outFile.getParentFile();
                if (dir != null && !dir.isDirectory() && !dir.mkdirs()) {
                    throw new IOException("mkdir failed: " + dir);
                }
                long total = info.size;
                long copied = 0;
                long lastUiUpdate = 0;
                try (InputStream in = getContentResolver().openInputStream(info.uri);
                     OutputStream os = new FileOutputStream(outFile)) {
                    if (in == null) throw new IOException("openInputStream retornou null");
                    byte[] buf = new byte[1024 * 1024];
                    int read;
                    while ((read = in.read(buf)) > 0) {
                        os.write(buf, 0, read);
                        copied += read;
                        if (total > 0 && copied - lastUiUpdate > 4 * 1024 * 1024) {
                            lastUiUpdate = copied;
                            final int pct = (int) Math.min(100, copied * 100 / total);
                            runOnUiThread(() -> {
                                progress.setIndeterminate(false);
                                progress.setProgress(pct);
                                statusText.setText(String.format(
                                        "Copiando %s … %d%%", info.displayName, pct));
                            });
                        }
                    }
                }
                if (total > 0 && outFile.length() != total) {
                    throw new IOException("tamanho final divergente (" + outFile.length()
                            + "/" + total + ")");
                }
                getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                        .putString(KEY_COPIED_NAME, info.displayName)
                        .putLong(KEY_COPIED_SIZE, total).apply();
                runOnUiThread(() -> startGame());
            } catch (Exception e) {
                outFile.delete();
                final String msg = e.getMessage();
                runOnUiThread(() -> fail("Falha ao copiar a imagem: " + msg));
            }
        }, "disc-copy");
        worker.start();
    }

    private void startGame() {
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }

    private void fail(String message) {
        progress.setVisibility(View.GONE);
        pickLayout.setVisibility(View.GONE);
        statusText.setText(message + "\n\nToque em \"Escolher pasta\" para tentar novamente.");
        pickLayout.setVisibility(View.VISIBLE);
        ((Button) pickLayout.getChildAt(1)).setText("Tentar de novo");
    }
}
