#pragma once

#include <Crypto/Commitment.h>
#include <Crypto/PublicKey.h>
#include <Crypto/Signature.h>
#include <Core/Util/JsonUtil.h>
#include <json/json.h>
#include <stdint.h>
#include <optional>

class ParticipantData
{
public:
	ParticipantData(
		const uint64_t participantId,
		const PublicKey& publicBlindExcess,
		const PublicKey& publicNonce
	)
		: m_participantId(participantId),
		m_publicBlindExcess(publicBlindExcess),
		m_publicNonce(publicNonce)
	{

	}

	ParticipantData(
		const uint64_t participantId,
		PublicKey&& publicBlindExcess,
		PublicKey&& publicNonce,
		std::optional<CompactSignature>&& partialSigOpt,
		std::optional<std::string>&& messageOpt,
		std::optional<CompactSignature>&& messageSigOpt
	) 
		: m_participantId(participantId),
		m_publicBlindExcess(publicBlindExcess), 
		m_publicNonce(publicNonce),
		m_partialSignatureOpt(std::move(partialSigOpt)),
		m_messageOpt(std::move(messageOpt)),
		m_messageSignatureOpt(std::move(messageSigOpt))
	{

	}

	bool operator==(const ParticipantData& rhs) const noexcept
	{
		return m_participantId == rhs.m_participantId &&
			m_publicBlindExcess == rhs.m_publicBlindExcess &&
			m_publicNonce == rhs.m_publicNonce &&
			m_partialSignatureOpt == rhs.m_partialSignatureOpt &&
			m_messageOpt == rhs.m_messageOpt &&
			m_messageSignatureOpt == rhs.m_messageSignatureOpt;
	}

	uint64_t GetParticipantId() const { return m_participantId; }
	const PublicKey& GetPublicBlindExcess() const { return m_publicBlindExcess; }
	const PublicKey& GetPublicNonce() const { return m_publicNonce; }
	const std::optional<CompactSignature>& GetPartialSignature() const { return m_partialSignatureOpt; }
	const std::optional<std::string>& GetMessageText() const { return m_messageOpt; }
	const std::optional<CompactSignature>& GetMessageSignature() const { return m_messageSignatureOpt; }

	void AddPartialSignature(const CompactSignature& signature) { m_partialSignatureOpt = std::make_optional(signature); }
	void AddMessage(const std::string& message, const CompactSignature& messageSignature)
	{
		m_messageOpt = std::make_optional(message);
		m_messageSignatureOpt = std::make_optional(messageSignature);
	}

	Json::Value ToJSON() const
	{
		Json::Value participantJSON;
		participantJSON["id"] = std::to_string(GetParticipantId());
		participantJSON["public_blind_excess"] = JsonUtil::ConvertToJSON(GetPublicBlindExcess().GetCompressedBytes().GetData());
		participantJSON["public_nonce"] = JsonUtil::ConvertToJSON(GetPublicNonce().GetCompressedBytes().GetData());
		participantJSON["part_sig"] = JsonUtil::ConvertToJSON(GetPartialSignature());
		participantJSON["message"] = JsonUtil::ConvertToJSON(GetMessageText());
		participantJSON["message_sig"] = JsonUtil::ConvertToJSON(GetMessageSignature());
		return participantJSON;
	}

	static ParticipantData FromJSON(const Json::Value& participantJSON)
	{
		const uint64_t participantId = JsonUtil::GetRequiredUInt64(participantJSON, "id");
		PublicKey publicBlindExcess = JsonUtil::GetPublicKey(participantJSON, "public_blind_excess");
		PublicKey publicNonce = JsonUtil::GetPublicKey(participantJSON, "public_nonce");
		std::optional<CompactSignature> partialSigOpt = JsonUtil::GetSignatureOpt(participantJSON, "part_sig");
		std::optional<std::string> messageOpt = JsonUtil::GetStringOpt(participantJSON, "message");
		std::optional<CompactSignature> messageSigOpt = JsonUtil::GetSignatureOpt(participantJSON, "message_sig");

		return ParticipantData(participantId, std::move(publicBlindExcess), std::move(publicNonce), std::move(partialSigOpt), std::move(messageOpt), std::move(messageSigOpt));
	}

private:
	// Id of participant in the transaction. (For now, 0=sender, 1=rec)
	uint64_t m_participantId;
	// Public key corresponding to private blinding factor
	PublicKey m_publicBlindExcess;
	// Public key corresponding to private nonce
	PublicKey m_publicNonce;
	// Public partial signature
	std::optional<CompactSignature> m_partialSignatureOpt;
	// A message for other participants
	std::optional<std::string> m_messageOpt;
	// Signature, created with private key corresponding to 'public_blind_excess'
	std::optional<CompactSignature> m_messageSignatureOpt;
};