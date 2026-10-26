#pragma once

// Private simulation arithmetic; include after phoenix.content is imported.
namespace phoenix
{
struct RuleWorkInput
{
	std::int64_t rate = 0, credit = 0, purchased = 0, available = 0;
	bool electrical = false, blocked = false;
};
struct RuleWorkPayment
{
	std::int64_t output = 1000;
	std::vector<std::int64_t> debits, credits;
	bool shortage = false;
};

// Inputs refer to distinct physical stores/resources; the caller coalesces
// duplicates before quoting. Electrical work belongs to a prepared allocation,
// so a rejected full-policy quote requires discarding/requoting that allocation.
inline RuleWorkPayment quote_rule_work(std::span<const RuleWorkInput> inputs, bool proportional)
{
	if (inputs.size() > 4096)
		throw std::runtime_error("rules: work bundle exceeds supported input count");
	const auto add = [](std::int64_t a, std::int64_t b)
	{
		if (a < 0 || b < 0 || a > std::numeric_limits<std::int64_t>::max() - b)
			throw std::runtime_error("rules: prepaid work overflow");
		return a + b;
	};
	const auto multiply = [](std::int64_t a, std::int64_t b)
	{
		if (a < 0 || b < 0 || (b && a > std::numeric_limits<std::int64_t>::max() / b))
			throw std::runtime_error("rules: prepaid work overflow");
		return a * b;
	};
	RuleWorkPayment payment;
	payment.debits.resize(inputs.size());
	payment.credits.reserve(inputs.size());
	for (const auto &input : inputs)
	{
		if (input.rate < 0 || input.rate > resource_limit || input.available < 0 || input.available > resource_limit ||
		    input.credit < 0 || input.purchased < 0 || (!input.electrical && input.purchased))
			throw std::runtime_error("rules: invalid prepaid work input");
		const auto work =
		    add(input.credit, input.electrical ? input.purchased : multiply(input.available, resource_work_unit));
		if (input.rate)
			payment.output = std::min(payment.output, input.blocked ? std::int64_t{0} : work / input.rate);
		payment.credits.push_back(input.credit);
	}
	payment.shortage = payment.output < 1000;
	if (payment.shortage && !proportional)
	{
		payment.output = 0;
		return payment;
	}
	for (std::size_t i = 0; i < inputs.size(); ++i)
	{
		const auto &input = inputs[i];
		const auto required = multiply(input.rate, payment.output);
		if (input.electrical)
			payment.credits[i] = add(input.credit, input.purchased) - required;
		else
		{
			const auto outstanding = std::max(std::int64_t{0}, required - input.credit);
			// Division/remainder avoids overflowing a ceil-division numerator.
			payment.debits[i] = outstanding / resource_work_unit + (outstanding % resource_work_unit != 0);
			if (payment.debits[i] > input.available)
				throw std::logic_error("rules: prepaid work quote exceeds available stock");
			payment.credits[i] = add(input.credit, multiply(payment.debits[i], resource_work_unit)) - required;
		}
	}
	return payment;
}
} // namespace phoenix
