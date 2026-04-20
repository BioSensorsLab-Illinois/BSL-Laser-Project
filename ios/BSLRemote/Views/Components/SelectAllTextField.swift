import SwiftUI
import UIKit

/// Numeric text field that **selects all** of its current contents when the
/// operator taps it, so entering a new value doesn't require manually
/// deleting the old one. SwiftUI's stock `TextField` does not expose a
/// "select all on focus" behaviour — this wraps `UITextField` and calls
/// `selectAll(nil)` inside `textFieldDidBeginEditing`.
///
/// Used by the safety-parameters form (user directive 2026-04-19) and by
/// any future numeric field that takes full-replacement entry.
struct SelectAllTextField<Value: Equatable>: UIViewRepresentable {
    @Binding var value: Value
    var decimal: Bool
    var format: (Value) -> String
    var parse: (String) -> Value?
    var textColor: UIColor
    var alignment: NSTextAlignment = .right
    var font: UIFont = .monospacedDigitSystemFont(ofSize: 13, weight: .semibold)

    // Convenience for Double
    static func makeDouble(value: Binding<Double>, textColor: UIColor) -> SelectAllTextField<Double> {
        SelectAllTextField<Double>(
            value: value,
            decimal: true,
            format: { Self.formatDouble($0) },
            parse: { Double($0.replacingOccurrences(of: ",", with: ".")) },
            textColor: textColor
        )
    }

    // Convenience for Int
    static func makeInt(value: Binding<Int>, textColor: UIColor) -> SelectAllTextField<Int> {
        SelectAllTextField<Int>(
            value: value,
            decimal: false,
            format: { "\($0)" },
            parse: { Int($0) },
            textColor: textColor
        )
    }

    func makeUIView(context: Context) -> UITextField {
        let tf = UITextField()
        tf.font = font
        tf.textColor = textColor
        tf.textAlignment = alignment
        tf.borderStyle = .none
        tf.keyboardType = decimal ? .decimalPad : .numberPad
        tf.autocorrectionType = .no
        tf.autocapitalizationType = .none
        tf.smartInsertDeleteType = .no
        tf.adjustsFontForContentSizeCategory = true
        tf.setContentHuggingPriority(.defaultLow, for: .horizontal)
        tf.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        tf.delegate = context.coordinator
        tf.addTarget(context.coordinator, action: #selector(Coordinator.editingChanged(_:)), for: .editingChanged)
        tf.text = format(value)
        tf.inputAccessoryView = Self.makeKeyboardToolbar(target: context.coordinator,
                                                        action: #selector(Coordinator.dismiss))
        return tf
    }

    func updateUIView(_ uiView: UITextField, context: Context) {
        context.coordinator.parent = self
        uiView.textColor = textColor
        // Only overwrite text when we aren't actively editing — otherwise the
        // caret would jump while the user is typing.
        if !uiView.isFirstResponder {
            let expected = format(value)
            if uiView.text != expected { uiView.text = expected }
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator(parent: self) }

    final class Coordinator: NSObject, UITextFieldDelegate {
        var parent: SelectAllTextField

        init(parent: SelectAllTextField) { self.parent = parent }

        func textFieldDidBeginEditing(_ textField: UITextField) {
            // Tiny defer — iOS needs the text-field edit session to be
            // fully live before `selectAll(_:)` picks up the range.
            DispatchQueue.main.async { textField.selectAll(nil) }
        }

        @objc func editingChanged(_ textField: UITextField) {
            guard let raw = textField.text, let v = parent.parse(raw) else { return }
            parent.value = v
        }

        func textFieldDidEndEditing(_ textField: UITextField) {
            // Normalize on blur so "5." becomes "5", etc.
            textField.text = parent.format(parent.value)
        }

        @objc func dismiss() {
            UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder),
                                            to: nil, from: nil, for: nil)
        }
    }

    /// "Done" toolbar above the numeric keypad — iOS number pads don't
    /// ship with a return key, so we synthesise one. Critical on iPhone
    /// where the decimal keypad otherwise has no way to dismiss.
    private static func makeKeyboardToolbar(target: AnyObject, action: Selector) -> UIToolbar {
        let bar = UIToolbar(frame: CGRect(x: 0, y: 0, width: 320, height: 44))
        bar.sizeToFit()
        let spacer = UIBarButtonItem(barButtonSystemItem: .flexibleSpace, target: nil, action: nil)
        let done = UIBarButtonItem(title: "Done", style: .done, target: target, action: action)
        done.tintColor = UIColor(red: 1.00, green: 0.372, blue: 0.019, alpha: 1.0) // BSL.orange
        bar.items = [spacer, done]
        return bar
    }

    // MARK: - Formatting

    /// Format a Double so integer values don't show a trailing ".0" and
    /// fractional values use up to 4 decimal places (matches the precision
    /// the firmware persists for safety thresholds).
    private static func formatDouble(_ v: Double) -> String {
        if v.isNaN || v.isInfinite { return "0" }
        if floor(v) == v && abs(v) < 1e9 { return String(Int(v)) }
        let nf = NumberFormatter()
        nf.numberStyle = .decimal
        nf.maximumFractionDigits = 4
        nf.minimumFractionDigits = 0
        nf.usesGroupingSeparator = false
        return nf.string(from: NSNumber(value: v)) ?? String(format: "%g", v)
    }
}
