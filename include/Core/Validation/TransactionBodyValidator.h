#pragma once

#include <Core/Models/TransactionBody.h>

class TransactionBodyValidator
{
public:
	void Validate(const TransactionBody& transactionBody, const bool withReward);

private:
	void ValidateWeight(const TransactionBody& transactionBody, const bool withReward);
	void VerifySorted(const TransactionBody& transactionBody);
	void VerifyCutThrough(const TransactionBody& transactionBody);
	void VerifyRangeProofs(const std::vector<TransactionOutput>& outputs);
};