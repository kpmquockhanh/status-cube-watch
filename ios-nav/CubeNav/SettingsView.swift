import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var ride: RideController
    @Environment(\.dismiss) private var dismiss
    @State private var url = ServerConfig.urlString
    @State private var key = ServerConfig.key
    @State private var saveFailed = false
    @State private var confirmForget = false
    @State private var forgotten = false

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("Server URL", text: $url)
                        .textInputAutocapitalization(.never).autocorrectionDisabled().keyboardType(.URL)
                    SecureField("Key", text: $key)
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                    if saveFailed {
                        Label("The key could not be saved in the Keychain. The old key is still in use. Try Save again.",
                              systemImage: "exclamationmark.triangle.fill")
                            .font(.footnote).foregroundStyle(Theme.bad)
                    }
                } header: {
                    Text("Nav server")
                } footer: {
                    Text("The key is kept in the iPhone Keychain, not in the app.")
                }
                .listRowBackground(Theme.surface)
                Section {
                    Button("Forget cube", role: .destructive) { confirmForget = true }
                        .disabled(ride.phase != .idle)
                    if forgotten { Text("Looking for a cube to pair with.").font(.footnote) }
                } header: {
                    Text("Cube")
                } footer: {
                    Text("After replacing the cube: the app stops reconnecting to the old one and searches for a new one. Also remove the old Claude Cube in iOS Settings > Bluetooth.")
                }
                .listRowBackground(Theme.surface)
                if let exp = ServerConfig.expiry() {
                    Section("App signing") { Text("Stops working on \(exp.formatted(date: .abbreviated, time: .shortened)). Rebuild from Xcode before then.") }
                        .listRowBackground(Theme.surface)
                }
            }
            .scrollContentBackground(.hidden)
            .background(Theme.bg)
            .navigationTitle("Settings")
            .confirmationDialog("Forget the cube this iPhone links to?", isPresented: $confirmForget, titleVisibility: .visible) {
                Button("Forget cube", role: .destructive) { ride.cubeLink.forget(); forgotten = true }
            }
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        ServerConfig.urlString = url
                        if ServerConfig.saveKey(key) { dismiss() } else { saveFailed = true }
                    }
                }
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
            }
        }
    }
}
