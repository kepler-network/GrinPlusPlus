#pragma once

#include "Wallet.h"

#include <Wallet/NodeClient.h>
#include <Config/Config.h>
#include <Core/Models/DTOs/OutputDTO.h>

class OutputRestorer
{
public:
	OutputRestorer(const Config& config, INodeClientConstPtr pNodeClient, const KeyChain& keyChain);

	std::vector<OutputDataEntity> FindAndRewindOutputs(
		Writer<IWalletDB> pBatch,
		const bool fromGenesis
	) const;

private:
	std::unique_ptr<OutputDataEntity> GetWalletOutput(
		const OutputDTO& output,
		const uint64_t currentBlockHeight
	) const;

	EOutputStatus DetermineStatus(
		const OutputDTO& output,
		const uint64_t currentBlockHeight
	) const;

	const Config& m_config;
	INodeClientConstPtr m_pNodeClient;
	const KeyChain& m_keyChain;
};