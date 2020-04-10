#include <catch.hpp>

#include "Helpers/TestNodeClient.h"

#include <Wallet/WalletManager.h>
#include <Config/ConfigLoader.h>
#include <uuid.h>

TEST_CASE("Wallet Creation/Deletion")
{
	ConfigPtr pConfig = ConfigLoader().Load(EEnvironmentType::MAINNET);
	std::shared_ptr<TestNodeClient> pNodeClient = std::make_shared<TestNodeClient>();
	IWalletManagerPtr pWalletManager = WalletAPI::CreateWalletManager(*pConfig, pNodeClient);

	// Create wallet
	const std::string username = uuids::to_string(uuids::uuid_system_generator()());
	std::optional<std::pair<SecureString, SessionToken>> wallet = pWalletManager->InitializeNewWallet(username, "Password1", 24);
	REQUIRE(wallet.has_value());

	// Validate seed words
	SecureString seedWords = pWalletManager->GetSeedWords(wallet.value().second);
	REQUIRE(wallet.value().first == seedWords);

	// Logout
	pWalletManager->Logout(wallet.value().second);

	// Delete wallet
	pWalletManager->DeleteWallet(username, "Password1");
}