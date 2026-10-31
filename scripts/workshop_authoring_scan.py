"""Small C++ lexical scanner for Workshop declaration integrity checks.

Preserves argument spans, handles comments, strings/raw strings and balanced
initializers. This is an architecture check, not a substitute for the compiler.
"""
from dataclasses import dataclass
import re


@dataclass(frozen=True)
class Token:
    text: str
    start: int
    end: int


def tokens(source):
    result = []
    at = 0
    while at < len(source):
        if source[at].isspace():
            at += 1
            continue
        if source.startswith("//", at):
            end = source.find("\n", at + 2)
            at = len(source) if end < 0 else end
            continue
        if source.startswith("/*", at):
            end = source.find("*/", at + 2)
            at = len(source) if end < 0 else end + 2
            continue
        raw = re.match(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(', source[at:])
        if raw:
            end = source.find(")" + raw[1] + '"', at + raw.end())
            end = len(source) if end < 0 else end + len(raw[1]) + 2
        elif source[at] in "\"'":
            quote = source[at]
            end = at + 1
            while end < len(source):
                if source[end] == "\\":
                    end += 2
                elif source[end] == quote:
                    end += 1
                    break
                else:
                    end += 1
        else:
            match = re.match(r'(?:[A-Za-z_]\w*|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[A-Za-z]*|::|->|==|!=|<=|>=|&&|\|\||\+=|-=|\*=|/=|.)', source[at:])
            end = at + len(match[0])
        result.append(Token(source[at:end], at, end))
        at = end
    return result


def matching(items, start):
    opening = {"(": ")", "[": "]", "{": "}"}
    stack = []
    for at in range(start, len(items)):
        text = items[at].text
        if text in opening:
            stack.append(opening[text])
        elif text in (")", "]", "}"):
            if not stack or stack.pop() != text:
                raise ValueError("Unbalanced C++ delimiters")
            if not stack:
                return at
    raise ValueError("Unclosed C++ initializer")


def calls(source, names):
    items = tokens(source)
    for at, token in enumerate(items[:-1]):
        if token.text not in names or items[at + 1].text != "(":
            continue
        if token.text == "string" and at >= 2 and items[at - 1].text == "::" and items[at - 2].text == "std":
            continue
        end = matching(items, at + 1)
        args = []
        begin = at + 2
        cursor = begin
        while cursor < end:
            if items[cursor].text in ("(", "[", "{"):
                cursor = matching(items, cursor) + 1
                continue
            if items[cursor].text == ",":
                args.append((items[begin].start, items[cursor].start))
                begin = cursor + 1
            cursor += 1
        if begin < end:
            args.append((items[begin].start, items[end].start))
        yield token.text, args, token.start, items[end].end


def static_value(expression):
    """Only literals and ordinary Json array/object constructors are data."""
    allowed = {"Json", "array", "object", "true", "false", "nullptr"}
    for token in tokens(expression):
        if re.fullmatch(r'[A-Za-z_]\w*', token.text) and token.text not in allowed:
            return False
    return True


def fixed_initializers(source):
    for key, start, end in fixed_assignments(source):
        expression = source[start:end].strip()
        if key.startswith('"') and (expression.startswith('{') or expression.startswith('Json::')):
            yield key, start, end


def fixed_assignments(source):
    items = tokens(source)
    for at, token in enumerate(items):
        if token.text != "=" or at < 3 or items[at - 1].text != "]":
            continue
        if items[at - 3].text != "[":
            continue
        start = at + 1
        if items[start].text == "{":
            end = matching(items, start)
        elif [x.text for x in items[start:start + 3]] in (["Json", "::", "array"], ["Json", "::", "object"]):
            if items[start + 3].text != "(":
                continue
            end = matching(items, start + 3)
        else:
            end = start
            while end < len(items) and items[end].text != ";":
                end += 1
            end -= 1
        if end + 1 >= len(items) or items[end + 1].text != ";":
            continue
        expression = source[items[start].start:items[end].end]
        if static_value(expression):
            yield items[at - 2].text, items[start].start, items[end].end


def raw_authoring_widgets(source, allowed):
    """Reject raw data widgets except audited presentation/command-argument bindings."""
    items = tokens(source)
    names, qualified = set(), set()
    for at in range(len(items) - 3):
        if items[at].text == "ImGui" and items[at + 1].text == "::" and items[at + 3].text == "(" and re.match(r"^(?:Input|Drag|Slider|ColorEdit|ColorPicker|Checkbox|Combo|RadioButton)", items[at + 2].text):
            names.add(items[at + 2].text)
            qualified.add(items[at + 2].start)
    errors = []
    for name, spans, start, end in calls(source, names):
        if start not in qualified:
            continue
        args = [source[slice(*span)].strip() for span in spans]
        pointer_at = 2 if name.startswith(("InputScalar", "DragScalar", "SliderScalar")) else 1
        binding = "".join(token.text for token in tokens(args[pointer_at])) if len(args) > pointer_at else ""
        if name not in allowed.get(binding, {}).get("widgets", []):
            errors.append(f"{name}({binding}): authored controls require a shared descriptor or action")
    return errors
