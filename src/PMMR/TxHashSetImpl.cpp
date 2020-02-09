#include "TxHashSetImpl.h"
#include "TxHashSetValidator.h"
#include "Common/MMRUtil.h"
#include "Common/MMRHashUtil.h"

#include <Common/Util/ThreadUtil.h>
#include <Common/Util/HexUtil.h>
#include <Common/Util/FileUtil.h>
#include <Common/Util/StringUtil.h>
#include <BlockChain/BlockChainServer.h>
#include <Database/BlockDb.h>
#include <Infrastructure/Logger.h>
#include <P2P/SyncStatus.h>
#include <thread>

TxHashSet::TxHashSet(
	std::shared_ptr<KernelMMR> pKernelMMR,
	std::shared_ptr<OutputPMMR> pOutputPMMR,
	std::shared_ptr<RangeProofPMMR> pRangeProofPMMR,
	BlockHeaderPtr pBlockHeader)
	: m_pKernelMMR(pKernelMMR),
	m_pOutputPMMR(pOutputPMMR),
	m_pRangeProofPMMR(pRangeProofPMMR),
	m_pBlockHeader(pBlockHeader),
	m_pBlockHeaderBackup(pBlockHeader)
{

}

bool TxHashSet::IsUnspent(const OutputLocation& location) const
{
	return m_pOutputPMMR->IsUnpruned(location.GetMMRIndex());
}

bool TxHashSet::IsValid(std::shared_ptr<const IBlockDB> pBlockDB, const Transaction& transaction) const
{
	// Validate inputs
	const uint64_t maximumBlockHeight = (std::max)(m_pBlockHeader->GetHeight() + 1, Consensus::COINBASE_MATURITY) - Consensus::COINBASE_MATURITY;
	for (const TransactionInput& input : transaction.GetInputs())
	{
		const Commitment& commitment = input.GetCommitment();
		std::unique_ptr<OutputLocation> pOutputPosition = pBlockDB->GetOutputPosition(commitment);
		if (pOutputPosition == nullptr)
		{
			return false;
		}

		std::unique_ptr<OutputIdentifier> pOutput = m_pOutputPMMR->GetAt(pOutputPosition->GetMMRIndex());
		if (pOutput == nullptr || pOutput->GetCommitment() != commitment || pOutput->GetFeatures() != input.GetFeatures())
		{
			LOG_DEBUG_F("Output ({}) not found at mmrIndex ({})",  commitment, pOutputPosition->GetMMRIndex());
			return false;
		}

		if (input.GetFeatures() == EOutputFeatures::COINBASE_OUTPUT)
		{
			if (pOutputPosition->GetBlockHeight() > maximumBlockHeight)
			{
				LOG_INFO_F("Coinbase ({}) not mature", transaction);
				return false;
			}
		}
	}

	// Validate outputs
	for (const TransactionOutput& output : transaction.GetOutputs())
	{
		std::unique_ptr<OutputLocation> pOutputPosition = pBlockDB->GetOutputPosition(output.GetCommitment());
		if (pOutputPosition != nullptr)
		{
			std::unique_ptr<OutputIdentifier> pOutput = m_pOutputPMMR->GetAt(pOutputPosition->GetMMRIndex());
			if (pOutput != nullptr && pOutput->GetCommitment() == output.GetCommitment())
			{
				return false;
			}
		}
	}

	return true;
}

std::unique_ptr<BlockSums> TxHashSet::ValidateTxHashSet(const BlockHeader& header, const IBlockChainServer& blockChainServer, SyncStatus& syncStatus)
{
	std::unique_ptr<BlockSums> pBlockSums = nullptr;

	try
	{
		LOG_INFO("Validating TxHashSet for block " + header.GetHash().ToHex());
		pBlockSums = TxHashSetValidator(blockChainServer).Validate(*this, header, syncStatus);
		if (pBlockSums != nullptr)
		{
			LOG_INFO("Successfully validated TxHashSet");
		}
	}
	catch (...)
	{
		LOG_ERROR("Exception thrown while processing TxHashSet");
	}

	return pBlockSums;
}

bool TxHashSet::ApplyBlock(std::shared_ptr<IBlockDB> pBlockDB, const FullBlock& block)
{
	Roaring blockInputBitmap;

	// Prune inputs
	for (const TransactionInput& input : block.GetInputs())
	{
		const Commitment& commitment = input.GetCommitment();
		std::unique_ptr<OutputLocation> pOutputPosition = pBlockDB->GetOutputPosition(commitment);
		if (pOutputPosition == nullptr)
		{
			LOG_WARNING_F("Output position not found for commitment ({}) in block ({})", commitment, block);
			return false;
		}

		const uint64_t mmrIndex = pOutputPosition->GetMMRIndex();
		m_pOutputPMMR->Remove(mmrIndex);
		m_pRangeProofPMMR->Remove(mmrIndex);

		blockInputBitmap.add((uint32_t)(mmrIndex + 1));
	}

	pBlockDB->AddBlockInputBitmap(block.GetHash(), blockInputBitmap);

	// Append new outputs
	for (const TransactionOutput& output : block.GetOutputs())
	{
		std::unique_ptr<OutputLocation> pOutputPosition = pBlockDB->GetOutputPosition(output.GetCommitment());
		if (pOutputPosition != nullptr)
		{
			if (pOutputPosition->GetMMRIndex() < m_pBlockHeader->GetOutputMMRSize())
			{
				std::unique_ptr<OutputIdentifier> pOutput = m_pOutputPMMR->GetAt(pOutputPosition->GetMMRIndex());
				if (pOutput != nullptr && pOutput->GetCommitment() == output.GetCommitment())
				{
					return false; // TODO: Handle this
				}
			}
		}

		const uint64_t mmrIndex = m_pOutputPMMR->GetSize();
		const uint64_t blockHeight = block.GetHeight();

		m_pOutputPMMR->Append(OutputIdentifier::FromOutput(output));
		m_pRangeProofPMMR->Append(output.GetRangeProof());

		pBlockDB->AddOutputPosition(output.GetCommitment(), OutputLocation(mmrIndex, blockHeight));
	}

	// Append new kernels
	for (const TransactionKernel& kernel : block.GetKernels())
	{
		m_pKernelMMR->ApplyKernel(kernel);
	}

	m_pBlockHeader = block.GetBlockHeader();

	return true;
}

bool TxHashSet::ValidateRoots(const BlockHeader& blockHeader) const
{
	if (m_pKernelMMR->Root(blockHeader.GetKernelMMRSize()) != blockHeader.GetKernelRoot())
	{
		LOG_ERROR_F("Kernel root not matching for header ({})", blockHeader);
		return false;
	}

	Hash outputRoot = m_pOutputPMMR->Root(blockHeader.GetOutputMMRSize());
	if (blockHeader.GetVersion() < 3)
	{
		if (outputRoot != blockHeader.GetOutputRoot())
		{
			LOG_ERROR_F("Output root not matching for header ({})", blockHeader);
			return false;
		}
	}
	else
	{
		Hash UBMT = m_pOutputPMMR->UBMTRoot(MMRUtil::GetNumLeaves(blockHeader.GetOutputMMRSize() - 1));
		Hash merged = MMRHashUtil::HashParentWithIndex(outputRoot, UBMT, blockHeader.GetOutputMMRSize());
		if (merged != blockHeader.GetOutputRoot())
		{
			LOG_ERROR_F("Output root not matching for header ({}). Output: {}, UBMT: {}", blockHeader, outputRoot, UBMT);
			return false;
		}
	}

	if (m_pRangeProofPMMR->Root(blockHeader.GetOutputMMRSize()) != blockHeader.GetRangeProofRoot())
	{
		LOG_ERROR_F("RangeProof root not matching for header ({})", blockHeader);
		return false;
	}

	return true;
}

void TxHashSet::SaveOutputPositions(std::shared_ptr<IBlockDB> pBlockDB, const BlockHeader& blockHeader, const uint64_t firstOutputIndex)
{
	const uint64_t size = blockHeader.GetOutputMMRSize();
	for (uint64_t mmrIndex = firstOutputIndex; mmrIndex < size; mmrIndex++)
	{
		std::unique_ptr<OutputIdentifier> pOutput = m_pOutputPMMR->GetAt(mmrIndex);
		if (pOutput != nullptr)
		{
			pBlockDB->AddOutputPosition(pOutput->GetCommitment(), OutputLocation(mmrIndex, blockHeader.GetHeight()));
		}
	}
}

std::vector<Hash> TxHashSet::GetLastKernelHashes(const uint64_t numberOfKernels) const
{
	return m_pKernelMMR->GetLastLeafHashes(numberOfKernels);
}

std::vector<Hash> TxHashSet::GetLastOutputHashes(const uint64_t numberOfOutputs) const
{
	return m_pOutputPMMR->GetLastLeafHashes(numberOfOutputs);
}

std::vector<Hash> TxHashSet::GetLastRangeProofHashes(const uint64_t numberOfRangeProofs) const
{
	return m_pRangeProofPMMR->GetLastLeafHashes(numberOfRangeProofs);
}

OutputRange TxHashSet::GetOutputsByLeafIndex(std::shared_ptr<const IBlockDB> pBlockDB, const uint64_t startIndex, const uint64_t maxNumOutputs) const
{
	const uint64_t outputSize = m_pOutputPMMR->GetSize();
	
	uint64_t leafIndex = startIndex;
	std::vector<OutputDTO> outputs;
	outputs.reserve(maxNumOutputs);
	while (outputs.size() < maxNumOutputs)
	{
		const uint64_t mmrIndex = MMRUtil::GetPMMRIndex(leafIndex++);
		if (mmrIndex >= outputSize)
		{
			break;
		}

		std::unique_ptr<OutputIdentifier> pOutput = m_pOutputPMMR->GetAt(mmrIndex);
		if (pOutput != nullptr)
		{
			std::unique_ptr<RangeProof> pRangeProof = m_pRangeProofPMMR->GetAt(mmrIndex);
			std::unique_ptr<OutputLocation> pOutputPosition = pBlockDB->GetOutputPosition(pOutput->GetCommitment());
			if (pRangeProof == nullptr || pOutputPosition == nullptr || pOutputPosition->GetMMRIndex() != mmrIndex)
			{
				throw TXHASHSET_EXCEPTION(StringUtil::Format("Failed to build OutputDTO at index {}", mmrIndex));
			}

			outputs.emplace_back(OutputDTO(false, *pOutput, *pOutputPosition, *pRangeProof));
		}
	}

	const uint64_t maxLeafIndex = MMRUtil::GetNumLeaves(outputSize - 1);
	const uint64_t lastRetrievedIndex = outputs.empty() ? 0 : MMRUtil::GetNumLeaves(outputs.back().GetLocation().GetMMRIndex());

	return OutputRange(maxLeafIndex, lastRetrievedIndex, std::move(outputs));
}

std::vector<OutputDTO> TxHashSet::GetOutputsByMMRIndex(std::shared_ptr<const IBlockDB> pBlockDB, const uint64_t startIndex, const uint64_t lastIndex) const
{
	std::vector<OutputDTO> outputs;
	uint64_t mmrIndex = startIndex;
	while (mmrIndex <= lastIndex)
	{
		std::unique_ptr<OutputIdentifier> pOutput = m_pOutputPMMR->GetAt(mmrIndex);
		if (pOutput != nullptr)
		{
			std::unique_ptr<RangeProof> pRangeProof = m_pRangeProofPMMR->GetAt(mmrIndex);
			std::unique_ptr<OutputLocation> pOutputPosition = pBlockDB->GetOutputPosition(pOutput->GetCommitment());

			outputs.emplace_back(OutputDTO(false, *pOutput, *pOutputPosition, *pRangeProof));
		}

		++mmrIndex;
	}

	return outputs;
}

void TxHashSet::Rewind(std::shared_ptr<const IBlockDB> pBlockDB, const BlockHeader& header)
{
	Roaring leavesToAdd;
	while (*m_pBlockHeader != header)
	{
		std::unique_ptr<Roaring> pBlockInputBitmap = pBlockDB->GetBlockInputBitmap(m_pBlockHeader->GetHash());
		if (pBlockInputBitmap != nullptr)
		{
			leavesToAdd |= *pBlockInputBitmap;
		}
		else
		{
			throw TXHASHSET_EXCEPTION(StringUtil::Format("Input bitmap not found for {}", *m_pBlockHeader));
		}

		m_pBlockHeader = pBlockDB->GetBlockHeader(m_pBlockHeader->GetPreviousBlockHash());
	}

	m_pKernelMMR->Rewind(header.GetKernelMMRSize());
	m_pOutputPMMR->Rewind(header.GetOutputMMRSize(), leavesToAdd);
	m_pRangeProofPMMR->Rewind(header.GetOutputMMRSize(), leavesToAdd);
}

void TxHashSet::Commit()
{
	this->m_pKernelMMR->Commit();
	this->m_pOutputPMMR->Commit();
	this->m_pRangeProofPMMR->Commit();
	//std::vector<std::thread> threads;
	//threads.emplace_back(std::thread([this] { this->m_pKernelMMR->Commit(); }));
	//threads.emplace_back(std::thread([this] { this->m_pOutputPMMR->Commit(); }));
	//threads.emplace_back(std::thread([this] { this->m_pRangeProofPMMR->Commit(); }));
	//ThreadUtil::JoinAll(threads);

	m_pBlockHeaderBackup = m_pBlockHeader;
}

void TxHashSet::Rollback() noexcept
{
	m_pKernelMMR->Rollback();
	m_pOutputPMMR->Rollback();
	m_pRangeProofPMMR->Rollback();
	m_pBlockHeader = m_pBlockHeaderBackup;
}

void TxHashSet::Compact()
{
	// TODO: Implement
}