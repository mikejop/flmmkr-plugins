// Renders the "FC" icon. Usage: swift make_icon.swift out.png size
import AppKit
let a = CommandLine.arguments
let size = CGFloat(Int(a[2])!)
let img = NSImage(size: NSSize(width: size, height: size))
img.lockFocus()
let r = NSRect(x: 0, y: 0, width: size, height: size)
NSColor(red: 74/255, green: 44/255, blue: 85/255, alpha: 1).setFill()
NSBezierPath(roundedRect: r.insetBy(dx: size*0.04, dy: size*0.04), xRadius: size*0.22, yRadius: size*0.22).fill()
let ps = NSMutableParagraphStyle(); ps.alignment = .center
let font = NSFont.systemFont(ofSize: size*0.50, weight: .heavy)
let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: NSColor(red: 254/255, green: 249/255, blue: 122/255, alpha: 1), .paragraphStyle: ps, .kern: -size*0.02]
let s = NSAttributedString(string: "FC", attributes: attrs)
let h = s.size().height
s.draw(in: NSRect(x: 0, y: (size-h)/2 - size*0.02, width: size, height: h))
img.unlockFocus()
let rep = NSBitmapImageRep(data: img.tiffRepresentation!)!
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: a[1]))
