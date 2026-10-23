import SwiftUI

struct SettingsView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var url = ServerConfig.urlString
    @State private var key = ServerConfig.key

    var body: some View {
        NavigationStack {
            Form {
                Section("Nav server") {
                    TextField("Server URL", text: $url)
                        .textInputAutocapitalization(.never).autocorrectionDisabled().keyboardType(.URL)
                    SecureField("Key", text: $key)
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                }
                Section { Text("The key is kept in the iPhone Keychain, not in the app.").font(.footnote) }
                if let exp = ServerConfig.expiry() {
                    Section("App signing") { Text("Stops working on \(exp.formatted(date: .abbreviated, time: .shortened)). Rebuild from Xcode before then.") }
                }
            }
            .navigationTitle("Settings")
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") { ServerConfig.urlString = url; ServerConfig.key = key; dismiss() }
                }
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
            }
        }
    }
}
