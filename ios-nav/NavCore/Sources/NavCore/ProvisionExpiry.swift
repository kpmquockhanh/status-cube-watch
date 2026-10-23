import Foundation

/// The expiry of an `embedded.mobileprovision`: the file is a CMS envelope around an XML plist,
/// so find the plist inside it and read `ExpirationDate`.
public func provisionExpiry(_ data: Data) -> Date? {
    guard let start = data.range(of: Data("<?xml".utf8)),
          let end = data.range(of: Data("</plist>".utf8), in: start.lowerBound..<data.endIndex) else { return nil }
    let plist = data[start.lowerBound..<end.upperBound]
    guard let obj = try? PropertyListSerialization.propertyList(from: Data(plist), format: nil),
          let dict = obj as? [String: Any] else { return nil }
    return dict["ExpirationDate"] as? Date
}
