#include <zeronote/crypto.h>
#include <zeronote/pdf_export.h>
#include <zeronote/text_codec.h>

#include <gtk/gtk.h>

#include <string>
#include <vector>

namespace {

struct AppState {
    GtkWidget* window = nullptr;
    GtkWidget* text_view = nullptr;
    GtkTextBuffer* buffer = nullptr;
    std::string filePath;
    bool modified = false;
    bool fileEncrypted = false;
    std::string sessionPassword;
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
    if (!g_app.sessionPassword.empty()) {
        for (char& ch : g_app.sessionPassword) ch = '\0';
        g_app.sessionPassword.clear();
    }
    g_app.fileEncrypted = false;
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
        g_free(path);
        return;
    }

    ClearEncryptionSession();
    std::string content;
    if (zeronote::crypto::IsEncryptedFile(bytes)) {
        g_free(path);
        return;
    }
    if (!zeronote::DecodeTextFromBytes(bytes, content)) {
        g_free(path);
        return;
    }

    SetEditorText(content);
    g_app.filePath = path;
    g_free(path);
    SetModified(false);
}

void OnSave(GtkButton*, gpointer) {
    if (g_app.filePath.empty() || g_app.fileEncrypted) {
        return;
    }
    if (zeronote::SaveTextFileUtf8(g_app.filePath, GetEditorText())) {
        SetModified(false);
    }
}

void OnExportPdf(GtkButton*, gpointer) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Export to PDF", GTK_WINDOW(g_app.window), GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Export", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "PDF");
    gtk_file_filter_add_pattern(filter, "*.pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return;
    }

    char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);
    if (!path) return;

    std::string error;
    zeronote::pdf::ExportTextToPdf(GetEditorText(), path, error);
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
    gtk_window_destroy(GTK_WINDOW(g_app.window));
}

GtkWidget* CreateMenuButton(const char* label, GCallback callback) {
    GtkWidget* button = gtk_button_new_with_label(label);
    g_signal_connect(button, "clicked", callback, nullptr);
    return button;
}

}

int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);

    g_app.window = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(g_app.window), 640, 480);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(toolbar, 6);
    gtk_widget_set_margin_end(toolbar, 6);
    gtk_widget_set_margin_top(toolbar, 6);

    gtk_box_append(GTK_BOX(toolbar), CreateMenuButton("New", G_CALLBACK(OnNew)));
    gtk_box_append(GTK_BOX(toolbar), CreateMenuButton("Open", G_CALLBACK(OnOpen)));
    gtk_box_append(GTK_BOX(toolbar), CreateMenuButton("Save", G_CALLBACK(OnSave)));
    gtk_box_append(GTK_BOX(toolbar), CreateMenuButton("Export PDF", G_CALLBACK(OnExportPdf)));

    GtkWidget* wrap = gtk_check_button_new_with_label("Word Wrap");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(wrap), TRUE);
    g_signal_connect(wrap, "toggled", G_CALLBACK(OnWordWrap), nullptr);
    gtk_box_append(GTK_BOX(toolbar), wrap);

    gtk_box_append(GTK_BOX(toolbar), CreateMenuButton("Quit", G_CALLBACK(OnQuit)));
    gtk_box_append(GTK_BOX(root), toolbar);

    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    g_app.text_view = gtk_text_view_new();
    g_app.buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_app.text_view));
    g_signal_connect(g_app.buffer, "changed", G_CALLBACK(OnBufferChanged), nullptr);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(g_app.text_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_app.text_view), GTK_WRAP_WORD);

    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, "textview { font-size: 12pt; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), g_app.text_view);
    gtk_box_append(GTK_BOX(root), scrolled);
    gtk_window_set_child(GTK_WINDOW(g_app.window), root);

    UpdateTitle();
    gtk_window_present(GTK_WINDOW(g_app.window));
    gtk_main();

    ClearEncryptionSession();
    return 0;
}