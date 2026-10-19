#pragma once
inline bool valid_value(const Value &value, Type type, bool missing = false)
{
	if (missing && value.type == Type::Missing)
		return true;
	if (value.type != type || type == Type::Missing)
		return false;
	if (type == Type::Boolean)
		return value.text.empty() && (value.number == 0 || value.number == 1);
	if (type == Type::Text)
		return value.number == 0 && value.text.size() <= 4096;
	return value.text.empty() && (type == Type::Integer || value.number > 0);
}
