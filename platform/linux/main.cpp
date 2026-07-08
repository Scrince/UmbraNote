#include <zeronote/crypto.h>
#include <zeronote/pdf_export.h>
#include <zeronote/text_codec.h>

#include <gtk/gtk.h>

#include <string>
#include <utility>
#include <vector>

namespace {

enum class PasswordDialogMode { Open, Save };

struct PasswordDialogParams {
    PasswordDialogMode mode = PasswordDialogMode::Open;
    bool require_keyfile = false;
    bool paranoid_kdf = true;
    std::string password;
    std::vector<uint8_t> keyfile;
};

struct AppState {
    GtkWidget* window = nullptr;
    GtkWidget* text_view = nullptr;
    GtkTextBuffer* buffer = nullptr;
    std::string filePath;
    bool modified = false;
    bool fileEncrypted = false;
    bool encryptedRequiresKeyfile = false;
    bool encryptedParanoidKdf = true;
    bool wordWrap = true;
};

AppState g_app;

std::string GetEditorText() {
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(g_app.buffer, &start, &end);
    char* text = gtk_text_buffer_get_text(g_app.buffer, &start, &end, FALSE);
    std::string out = text ? text : "";
    g_free(text);
    return out;
}

void SetEditorText(const std::string& text) {
    gtk_text_buffer_set_text(g_app.buffer, text.c_str(), static_cast<int>(text.size()));
}

void UpdateTitle() {
    std::string name = "Untitled";
    if (!g_app.filePath.empty()) {
        const size_t pos = g_app.filePath.find_last_of("/\\");
        name = pos == std::string::npos ? g_app.filePath : g_app.filePath.substr(pos + 1);
    }
    const std::string encrypted = g_app.fileEncrypted ? " [Encrypted]" : "";
    const std::string title = (g_app.modified ? "*" : "") + name + encrypted + " - UmbraNote";
    gtk_window_set_title(GTK_WINDOW(g_app.window), title.c_str());
}

void SetModified(bool modified) {
    g_app.modified = modified;
    UpdateTitle();
}

void ClearEncryptionSession() {
    g_app.fileEncrypted = false;
    g_app.encryptedRequiresKeyfile = false;
    g_app.encryptedParanoidKdf = true;
}

void ShowError(const char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(g_app.window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK, "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

bool ConfirmDiscardChanges() {
    if (!g_app.modified) return true;
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(g_app.window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO, "Discard unsaved changes?");
    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_YES;
}

zeronote::crypto::EncryptionOptions BuildEncryptionOptions(const PasswordDialogParams& params) {
    zeronote::crypto::EncryptionOptions options;
    options.keyfile = params.keyfile;
    options.paranoid_kdf = params.paranoid_kdf;
    return options;
}

bool LoadKeyfileBytes(const std::string& path, std::vector<uint8_t>& bytes, std::string& error) {
    if (!zeronote::ReadFileBytes(path, bytes)) {
        error = "Cannot read the selected keyfile.";
        return false;
    }
    if (bytes.size() < zeronote::crypto::kMinKeyfileSize) {
        error = "Keyfile must be at least 32 bytes.";
        return false;
    }
    return true;
}

struct PasswordDialogWidgets {
    GtkWidget* dialog = nullptr;
    GtkWidget* password = nullptr;
    GtkWidget* confirm = nullptr;
    GtkWidget* confirm_label = nullptr;
    GtkWidget* keyfile_path = nullptr;
    GtkWidget* keyfile_label = nullptr;
    GtkWidget* paranoid = nullptr;
    PasswordDialogParams* params = nullptr;
    bool finished = false;
    bool accepted = false;
};

void OnPasswordDialogResponse(GtkDialog* dialog, int response, gpointer user_data) {
    auto* widgets = static_cast<PasswordDialogWidgets*>(user_data);
    if (response != GTK_RESPONSE_OK) {
        widgets->finished = true;
        widgets->accepted = false;
        gtk_widget_destroy(GTK_WIDGET(dialog));
        return;
    }

    PasswordDialogParams result;
    result.mode = widgets->params->mode;
    result.require_keyfile = widgets->params->require_keyfile;
    result.paranoid_kdf =
        gtk_check_button_get_active(GTK_CHECK_BUTTON(widgets->paranoid)) != FALSE;

    const char* passwordText = gtk_entry_get_text(GTK_ENTRY(widgets->password));
    result.password = passwordText ? passwordText : "";
    if (result.password.empty()) {
        ShowError("Password cannot be empty.");
        return;
    }

    if (result.mode == PasswordDialogMode::Save) {
        const char* confirmText = gtk_entry_get_text(GTK_ENTRY(widgets->confirm));
        const std::string confirm = confirmText ? confirmText : "";
        if (result.password != confirm) {
            ShowError("Passwords do not match.");
            return;
        }
    }

    const char* keyfileText = gtk_entry_get_text(GTK_ENTRY(widgets->keyfile_path));
    const std::string keyfilePath = keyfileText ? keyfileText : "";
    if (!keyfilePath.empty()) {
        std::string keyfileError;
        if (!LoadKeyfileBytes(keyfilePath, result.keyfile, keyfileError)) {
            ShowError(keyfileError.c_str());
            return;
        }
    }

    if (result.paranoid_kdf && result.keyfile.empty()) {
        ShowError("High-security mode requires a keyfile in addition to the password.");
        return;
    }
    if (result.require_keyfile && result.keyfile.empty()) {
        ShowError("This file requires the original keyfile.");
        return;
    }

    if (result.mode == PasswordDialogMode::Save) {
        std::string passwordError;
        if (!zeronote::crypto::ValidateEncryptionPassword(
                result.password, result.paranoid_kdf, passwordError)) {
            ShowError(passwordError.c_str());
            return;
        }
    }

    *widgets->params = std::move(result);
    widgets->finished = true;
    widgets->accepted = true;
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void OnKeyfileBrowseClicked(GtkButton*, gpointer user_data) {
    auto* widgets = static_cast<PasswordDialogWidgets*>(user_data);
    GtkWidget* chooser = gtk_file_chooser_dialog_new(
        "Select Keyfile", GTK_WINDOW(widgets->dialog), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (path) {
            gtk_entry_set_text(GTK_ENTRY(widgets->keyfile_path), path);
            g_free(path);
        }
    }
    gtk_widget_destroy(chooser);
}

bool PromptPassword(PasswordDialogParams& params) {
    PasswordDialogWidgets widgets;
    widgets.params = &params;

    widgets.dialog = gtk_dialog_new();
    gtk_window_set_title(
        GTK_WINDOW(widgets.dialog),
        params.mode == PasswordDialogMode::Open ? "Unlock Protected Note" : "Protect Note");
    gtk_window_set_transient_for(GTK_WINDOW(widgets.dialog), GTK_WINDOW(g_app.window));
    gtk_window_set_modal(GTK_WINDOW(widgets.dialog), TRUE);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(widgets.dialog));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    GtkWidget* password_label = gtk_label_new("Password:");
    gtk_widget_set_halign(password_label, GTK_ALIGN_START);
    widgets.password = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(widgets.password), FALSE);
    gtk_entry_set_width_chars(GTK_ENTRY(widgets.password), 32);
    gtk_grid_attach(GTK_GRID(grid), password_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), widgets.password, 1, 0, 2, 1);

    widgets.confirm_label = gtk_label_new("Confirm:");
    gtk_widget_set_halign(widgets.confirm_label, GTK_ALIGN_START);
    widgets.confirm = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(widgets.confirm), FALSE);
    gtk_entry_set_width_chars(GTK_ENTRY(widgets.confirm), 32);
    gtk_grid_attach(GTK_GRID(grid), widgets.confirm_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), widgets.confirm, 1, 1, 2, 1);
    if (params.mode == PasswordDialogMode::Open) {
        gtk_widget_set_visible(widgets.confirm_label, FALSE);
        gtk_widget_set_visible(widgets.confirm, FALSE);
    }

    widgets.keyfile_label = gtk_label_new(
        params.require_keyfile ? "Keyfile:" : "Keyfile (optional):");
    gtk_widget_set_halign(widgets.keyfile_label, GTK_ALIGN_START);
    widgets.keyfile_path = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(widgets.keyfile_path), FALSE);
    GtkWidget* browse = gtk_button_new_with_label("Browse...");
    gtk_grid_attach(GTK_GRID(grid), widgets.keyfile_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), widgets.keyfile_path, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), browse, 2, 2, 1, 1);

    widgets.paranoid = gtk_check_button_new_with_label(
        "High-security mode (Argon2id + keyfile required)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(widgets.paranoid), params.paranoid_kdf);
    if (params.mode == PasswordDialogMode::Open) {
        gtk_widget_set_sensitive(widgets.paranoid, FALSE);
    }
    gtk_grid_attach(GTK_GRID(grid), widgets.paranoid, 0, 3, 3, 1);

    GtkWidget* hint = gtk_label_new(
        "Uses XChaCha20-Poly1305, Argon2id, and re-auth on save.");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_grid_attach(GTK_GRID(grid), hint, 0, 4, 3, 1);

    gtk_dialog_add_button(GTK_DIALOG(widgets.dialog), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(widgets.dialog), "_OK", GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(widgets.dialog), GTK_RESPONSE_OK);

    g_signal_connect(browse, "clicked", G_CALLBACK(OnKeyfileBrowseClicked), &widgets);
    g_signal_connect(widgets.dialog, "response", G_CALLBACK(OnPasswordDialogResponse), &widgets);

    gtk_widget_set_visible(widgets.dialog, TRUE);
    while (!widgets.finished) {
        g_main_context_iteration(nullptr, TRUE);
    }

    return widgets.accepted && !params.password.empty();
}

bool SaveEncryptedFile(const std::string& path, const std::string& text,
                       const PasswordDialogParams& auth, std::string& error) {
    std::vector<uint8_t> encrypted;
    std::string err;
    if (!zeronote::crypto::EncryptText(text, auth.password, encrypted, err,
                                       BuildEncryptionOptions(auth))) {
        error = err;
        return false;
    }
    return zeronote::WriteFileBytesAtomic(path, encrypted);
}

bool VerifyExistingEncryptedFile(const std::string& path, const PasswordDialogParams& auth,
                                 std::string& error) {
    std::vector<uint8_t> bytes;
    if (!zeronote::ReadFileBytes(path, bytes)) {
        error = "Cannot read the encrypted file before saving.";
        return false;
    }
    std::string plaintext;
    std::string err;
    if (!zeronote::crypto::DecryptText(bytes, auth.password, plaintext, err,
                                       BuildEncryptionOptions(auth))) {
        error = err.empty() ? "Password or keyfile does not match this file." : err;
        return false;
    }
    return true;
}

void OnBufferChanged(GtkTextBuffer*, gpointer) {
    if (!g_app.modified) {
        SetModified(true);
    }
}

void OnNew(GtkButton*, gpointer) {
    if (!ConfirmDiscardChanges()) return;
    SetEditorText("");
    g_app.filePath.clear();
    ClearEncryptionSession();
    SetModified(false);
}

void OnOpen(GtkButton*, gpointer) {
    if (!ConfirmDiscardChanges()) return;

    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Open File", GTK_WINDOW(g_app.window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Supported");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_filter_add_pattern(filter, "*.zro");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return;
    }

    char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);
    if (!path) return;

    std::vector<uint8_t> bytes;
    if (!zeronote::ReadFileBytes(path, bytes)) {
        ShowError("Cannot open the file.");
        g_free(path);
        return;
    }

    ClearEncryptionSession();
    std::string content;

    if (zeronote::crypto::IsEncryptedFile(bytes)) {
        zeronote::crypto::EncryptedFileInfo info;
        zeronote::crypto::GetEncryptedFileInfo(bytes, info);

        PasswordDialogParams auth;
        auth.mode = PasswordDialogMode::Open;
        auth.require_keyfile = info.requires_keyfile;
        auth.paranoid_kdf = info.paranoid_kdf;
        if (!PromptPassword(auth)) {
            g_free(path);
            return;
        }

        std::string plainUtf8;
        std::string error;
        if (!zeronote::crypto::DecryptText(bytes, auth.password, plainUtf8, error,
                                           BuildEncryptionOptions(auth))) {
            ShowError(error.c_str());
            g_free(path);
            return;
        }

        content = plainUtf8;
        g_app.fileEncrypted = true;
        g_app.encryptedRequiresKeyfile = info.requires_keyfile;
        g_app.encryptedParanoidKdf = info.paranoid_kdf;
    } else {
        if (!zeronote::DecodeTextFromBytes(bytes, content)) {
            ShowError("Cannot open the file.");
            g_free(path);
            return;
        }
    }

    SetEditorText(content);
    g_app.filePath = path;
    g_free(path);
    SetModified(false);
}

bool SavePlainFile(const std::string& path) {
    if (!zeronote::SaveTextFileUtf8(path, GetEditorText())) {
        ShowError("Cannot save the file.");
        return false;
    }
    return true;
}

void OnSave(GtkButton*, gpointer) {
    if (g_app.filePath.empty()) {
        OnSaveAs(nullptr, nullptr);
        return;
    }

    if (g_app.fileEncrypted) {
        PasswordDialogParams auth;
        auth.mode = PasswordDialogMode::Open;
        auth.require_keyfile = g_app.encryptedRequiresKeyfile;
        auth.paranoid_kdf = g_app.encryptedParanoidKdf;
        if (!PromptPassword(auth)) {
            return;
        }

        std::string error;
        if (!VerifyExistingEncryptedFile(g_app.filePath, auth, error)) {
            ShowError(error.empty() ? "Password or keyfile does not match this file."
                                    : error.c_str());
            return;
        }
        if (!SaveEncryptedFile(g_app.filePath, GetEditorText(), auth, error)) {
            ShowError(error.empty() ? "Cannot save the encrypted file." : error.c_str());
            return;
        }
        SetModified(false);
        return;
    }

    if (SavePlainFile(g_app.filePath)) {
        SetModified(false);
    }
}

void OnSaveAs(GtkButton*, gpointer) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Save As", GTK_WINDOW(g_app.window), GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Text Documents");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "Untitled.txt");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return;
    }

    char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);
    if (!path) return;

    if (SavePlainFile(path)) {
        g_app.filePath = path;
        ClearEncryptionSession();
        SetModified(false);
    }
    g_free(path);
}

void OnSaveEncryptedAs(GtkButton*, gpointer) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Save Encrypted As", GTK_WINDOW(g_app.window), GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Encrypted Notes");
    gtk_file_filter_add_pattern(filter, "*.zro");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "Untitled.zro");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return;
    }

    char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);
    if (!path) return;

    PasswordDialogParams auth;
    auth.mode = PasswordDialogMode::Save;
    auth.paranoid_kdf = true;
    if (!PromptPassword(auth)) {
        g_free(path);
        return;
    }

    std::string error;
    if (!SaveEncryptedFile(path, GetEditorText(), auth, error)) {
        ShowError(error.empty() ? "Cannot save the encrypted file." : error.c_str());
        g_free(path);
        return;
    }

    g_app.filePath = path;
    g_app.fileEncrypted = true;
    g_app.encryptedRequiresKeyfile = !auth.keyfile.empty();
    g_app.encryptedParanoidKdf = auth.paranoid_kdf;
    SetModified(false);
    g_free(path);
}

void OnExportPdf(GtkButton*, gpointer) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Export to PDF", GTK_WINDOW(g_app.window), GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Export", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "PDF");
    gtk_file_filter_add_pattern(filter, "*.pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "Untitled.pdf");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return;
    }

    char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);
    if (!path) return;

    std::string error;
    if (!zeronote::pdf::ExportTextToPdf(GetEditorText(), path, error)) {
        ShowError(error.empty() ? "Cannot export the PDF file." : error.c_str());
    }
    g_free(path);
}

void OnWordWrap(GtkCheckButton* button, gpointer) {
    g_app.wordWrap = gtk_check_button_get_active(GTK_CHECK_BUTTON(button)) != FALSE;
    gtk_text_view_set_wrap_mode(
        GTK_TEXT_VIEW(g_app.text_view),
        g_app.wordWrap ? GTK_WRAP_WORD : GTK_WRAP_NONE);
}

void OnQuit(GtkButton*, gpointer) {
    if (!ConfirmDiscardChanges()) return;
    gtk_widget_destroy(g_app.window);
}

gboolean OnWindowDelete(GtkWidget*, GdkEvent*, gpointer) {
    return ConfirmDiscardChanges() ? FALSE : TRUE;
}

GtkWidget* CreateMenuButton(const char* label, GCallback callback) {
    GtkWidget* button = gtk_button_new_with_label(label);
    g_signal_connect(button, "clicked", callback, nullptr);
    return button;
}

}

int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);

    g_app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(g_app.window), 640, 480);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(toolbar, 6);
    gtk_widget_set_margin_end(toolbar, 6);
    gtk_widget_set_margin_top(toolbar, 6);

    gtk_box_pack_start(GTK_BOX(toolbar), CreateMenuButton("New", G_CALLBACK(OnNew)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), CreateMenuButton("Open", G_CALLBACK(OnOpen)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), CreateMenuButton("Save", G_CALLBACK(OnSave)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), CreateMenuButton("Save As", G_CALLBACK(OnSaveAs)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), CreateMenuButton("Save Encrypted", G_CALLBACK(OnSaveEncryptedAs)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), CreateMenuButton("Export PDF", G_CALLBACK(OnExportPdf)), FALSE, FALSE, 0);

    GtkWidget* wrap = gtk_check_button_new_with_label("Word Wrap");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(wrap), TRUE);
    g_signal_connect(wrap, "toggled", G_CALLBACK(OnWordWrap), nullptr);
    gtk_box_pack_start(GTK_BOX(toolbar), wrap, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(toolbar), CreateMenuButton("Quit", G_CALLBACK(OnQuit)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 0);

    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    g_app.text_view = gtk_text_view_new();
    g_app.buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_app.text_view));
    g_signal_connect(g_app.buffer, "changed", G_CALLBACK(OnBufferChanged), nullptr);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(g_app.text_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_app.text_view), GTK_WRAP_WORD);

    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, "textview { font-size: 12pt; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_container_add(GTK_CONTAINER(scrolled), g_app.text_view);
    gtk_box_pack_start(GTK_BOX(root), scrolled, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(g_app.window), root);
    g_signal_connect(g_app.window, "delete-event", G_CALLBACK(OnWindowDelete), nullptr);

    UpdateTitle();
    gtk_widget_show_all(g_app.window);
    gtk_window_present(GTK_WINDOW(g_app.window));
    gtk_main();

    ClearEncryptionSession();
    return 0;
}
