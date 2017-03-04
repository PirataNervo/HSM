// HSMApp.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <windows.h>
#include <stdint.h>
#include "UART.h"
#include "mbedtls/config.h"

#if defined(MBEDTLS_PLATFORM_C)
#include "mbedtls/platform.h"
#else
#include <stdio.h>
#define mbedtls_printf     printf
#endif

#define VERBOSE 0

#if !defined(MBEDTLS_ECDH_C) || \
    !defined(MBEDTLS_ECP_DP_CURVE25519_ENABLED) || \
    !defined(MBEDTLS_ENTROPY_C) || !defined(MBEDTLS_CTR_DRBG_C)
int main(void)
{
	mbedtls_printf("MBEDTLS_ECDH_C and/or "
		"MBEDTLS_ECP_DP_CURVE25519_ENABLED and/or "
		"MBEDTLS_ENTROPY_C and/or MBEDTLS_CTR_DRBG_C "
		"not defined\n");
	return(0);
}
#else

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"

#include <string.h>

/*==============================================================================
Macro
*/
#define   VALID                   0U
#define   INVALID                 1U
#define   AES_KEY_REGEN_CNT       10u
#define   KEY_LENGTH_128          32u
#define   KEY_LENGTH_256          64u
#define   KEY_HMAC                6u
#define   INIT_VECTOR_VALUE       8u
#define   AES_DECRYPT             1u
#define   AES_ENCRYPT             2u
#define   ENTER                   13u
#define   DUMMY_DATA              0u
#define   ASCII_32_CHARACTERS     32u
#define   ASCII_64_CHARACTERS     (ASCII_32_CHARACTERS * 2)
#define   ASCII_128_CHARACTERS    (ASCII_32_CHARACTERS * 4)
#define   DATA_LENGTH_128_BITS    ASCII_32_CHARACTERS
#define   DATA_LENGTH_256_BITS    ASCII_64_CHARACTERS
#define   DATA_LENGTH_512_BITS    ASCII_128_CHARACTERS
#define   DATA_LENGTH_16_BYTES    (ASCII_32_CHARACTERS / 2)
#define   DATA_LENGTH_32_BYTES    (ASCII_64_CHARACTERS / 2)
#define   DATA_LENGTH_64_BYTES    (ASCII_128_CHARACTERS / 2)
#define   BUFSIZE         1024

const uint8_t g_separator[] =
"------------------------------------------------------------------------------\r\n";

uint8_t buffer[4096];
uint32_t len;

UART * comm;

void NEW_USER();
void EXPORT_USERS();
void TIME_SEND();
void GEN_KEYS();
void DATA_SIGN();
void DATA_VERIFY();
void GET_CERT();
void USER_CERT();
void START_SESSION();
void LOG_ADD();

int main()
{
	printf("%s", g_separator);
	printf("\t\tHSM Application\r\n");
	printf("\tPress ENTER to initiate communication.\r\n");
	printf("%s", g_separator);

	getchar();

	// Initiate UART communication
	comm = new UART();
	comm->init();
	//comm->connect();

	// Send command request
	/*comm->reqCommand();

	TIME_SEND();*/
	
	comm->reqCommand();
	START_SESSION();

	comm->reqCommand();
	LOG_ADD();
	comm->reqCommand();
	LOG_ADD();
	/*comm->reqCommand();
	LOG_ADD();
	comm->reqCommand();
	LOG_ADD();
	comm->reqCommand();
	LOG_ADD();
	comm->reqCommand();
	LOG_ADD();*/

	/*comm->reqCommand();
	NEW_USER();*/

	comm->reqCommand();
	USER_CERT();

	/*comm->reqCommand();

	GET_CERT();

	comm->reqCommand();

	NEW_USER();

	comm->reqCommand();

	DATA_SIGN();

	// 'buffer' contains the signature (25B length as of time of writing)
	// Run DATA_VERIFY
	comm->reqCommand();

	DATA_VERIFY();*/

	/*comm->reqCommand();

	GEN_KEYS();*/

	printf("\nPress ENTER to continue.\n");

	getchar();

    return 0;
}

// Send new user request
void TIME_SEND()
{
	printf("Sending SEND_TIME command...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "TIME_SEND");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK.\n");

	// Now it expects a timestamp
	printf("Sending DATA...");
	memset(buffer, 0, sizeof(buffer));
	uint32_t t = time(NULL);

	// Place the size into an array
	unsigned char timestamp[4];
	timestamp[0] = t & 0x000000FF;
	timestamp[1] = (t & 0x0000FF00) >> 8;
	timestamp[2] = (t & 0x00FF0000) >> 16;
	timestamp[3] = (t & 0xFF000000) >> 24;
	comm->send(timestamp, 4);
	printf("OK\n");
}

// Send new user request
void NEW_USER()
{
	printf("Sending USER_NEW command...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "USER_NEW");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK.\n");

	// Now it expects:
	// ADMIN_PIN | USER_PIN
	printf("Sending DATA...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "12345678912345678912345678912345");
	sprintf_s((char*)buffer, sizeof(buffer), "%s12345678912345678912345678912341", buffer);
	comm->send(buffer, strlen((char*)buffer));
	printf("OK\n");

	// Wait for 'SUCCESS_ID'
	printf("Receiving SUCCESS_ID...");
	memset(buffer, 0, sizeof(buffer));
	comm->receive(&buffer[0], 4096);
	printf("OK: %s\n", buffer);

	// Wait for ID (1B)
	printf("Receiving ID...");
	memset(buffer, 0, sizeof(buffer));
	comm->receive(&buffer[0], 4096);
	printf("OK: %d\n", buffer[0]);
}

// get certificate for a public key
void GET_CERT()
{
	printf("Sending CRT_REQUEST command...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "CRT_REQUEST");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK.\n");

	// Now it expects:
	// 32B ADMIN_PIN | subject name + \0 or 0 (should be the same thing) | 1B for key usage
	printf("Sending DATA...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "12345678912345678912345678912345");
	sprintf_s((char*)buffer, sizeof(buffer), "%sCN=Diogo Parrinha, O=CIIST, C=PT", buffer);
	buffer[strlen((char*)buffer)] = '\0';
	buffer[strlen((char*)buffer) + 1] = 1; // non-repudiation
	comm->send(buffer, strlen((char*)buffer)+1+1); // +1 because of \0 at the end of subject name and +1 becaues of key usage
	printf("OK\n");

	// Send public key
	printf("Sending public key...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "-----BEGIN PUBLIC KEY-----\nMEkwEwYHKoZIzj0CAQYIKoZIzj0DAQEDMgAExVk6YzJ47/cxxyB9nTNSf0Q49Bz/\nnKbxtniyyrIo5ABC6cyYbAsFdmQAFTaeZ8l8\n-----END PUBLIC KEY-----\n");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK\n");

	// Wait for 'CERTIFICATE'
	printf("Receiving CERTIFICATE...");
	memset(buffer, 0, sizeof(buffer));
	comm->receive(&buffer[0], 4096);
	printf("OK: %s\n", buffer);

	// Wait for actual certificate
	printf("Receiving certificate content...");
	memset(buffer, 0, sizeof(buffer));
	comm->receive(&buffer[0], 4096);
	printf("OK.\n");
}

// get certificate for a public key
void USER_CERT()
{
	printf("Sending USER_CERT command...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "USER_CERT");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK.\n");

	// Now it expects:
	// 1B ID
	printf("Sending DATA...");
	memset(buffer, 0, sizeof(buffer));
	buffer[0] = 1;
	comm->send(buffer, 1);
	printf("OK\n");

	// Wait for 'CERTIFICATE'
	printf("Receiving CERTIFICATE...");
	memset(buffer, 0, sizeof(buffer));
	comm->receive(&buffer[0], 4096);
	printf("OK: %s\n", buffer);

	// Wait for actual certificate
	printf("Receiving certificate content...");
	memset(buffer, 0, sizeof(buffer));
	comm->receive(&buffer[0], 4096);
	printf("OK.\n");
}

// works for len < 256
uint8_t get_len(uint8_t * buffer)
{
	uint8_t i = 0;
	while (1)
	{
		if (buffer[i] == 0)
			break;

		i++;
	}

	return i;
}

// Send new user request
void GEN_KEYS()
{
	printf("Sending USER_GENKEYS command...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "USER_GENKEYS");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK.\n");

	// Now it expects:
	// ADMIN_PIN | USER_ID
	printf("Sending DATA...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "12345678912345678912345678912345");
	sprintf_s((char*)buffer, sizeof(buffer), "%s", buffer);
	int l = strlen((char*)buffer)+1;
	buffer[strlen((char*)buffer)] = 1; // ID
	comm->send(buffer, l);
	printf("OK\n");

	// Wait for 'SUCCESS_CERT'
	printf("Receiving CERTIFICATE...");
	memset(buffer, 0, sizeof(buffer));
	comm->receive(&buffer[0], 4096);
	printf("OK: %s\n", buffer);
}

// Send hash to be signed
void DATA_SIGN()
{
	// Calculate SHA-256 of "123456"
	uint8_t msg[6] = { '1','2','3','4','5','6'};
	unsigned char digest[32] = { 0 };
	mbedtls_sha256(msg, 6, digest, 0);

	printf("Sending DTSN_SIGN command...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "DTSN_SIGN");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK.\n");

	// Now it expects:
	// USER_PIN | USER_ID | 32B_HASH
	printf("Sending DATA...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "12345678912345678912345678912341"); // user pin
	buffer[32] = 1; // ID
	memcpy(&buffer[33], digest, 32);
	comm->send(buffer, 32+1+32);
	printf("OK\n");

	// Wait for 'SIGNATURE'
	printf("Receiving SIGNATURE...");
	memset(buffer, 0, sizeof(buffer));
	comm->receive(&buffer[0], 4096);
	printf("OK: %s\n", buffer);

	// Wait for actual signature
	printf("Receiving actual signature...");
	memset(buffer, 0, sizeof(buffer));
	len = comm->receive(&buffer[0], 4096);
	printf("OK: %d\n", len);
}

// Send hash along with signature to be verified
// must be executed after DATA_SIGN because buffer must contain the signature
void DATA_VERIFY()
{
	// 'buffer' should contain the signature
	uint8_t signature[512] = { 0 };
	uint8_t sig_len = len;
	memcpy(signature, buffer, sig_len);

	// Calculate SHA-256 of "123456"
	uint8_t msg[6] = { '1','2','3','4','5','6' };
	unsigned char digest[32] = { 0 };
	mbedtls_sha256(msg, 6, digest, 0);

	printf("Sending DTSN_VERIFY command...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "DTSN_VERIFY");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK.\n");

	// Now it expects:
	// USER_PIN | USER_ID | 32B_HASH | 1B SIGNATURE SIZE
	printf("Sending DATA...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "12345678912345678912345678912341"); // user pin
	buffer[32] = 1; // ID
	memcpy(&buffer[33], digest, 32);
	buffer[65] = sig_len;
	comm->send(buffer, 32 + 1 + 32 + 1);
	printf("OK\n");

	// Send signature
	printf("Sending SIGNATURE...");
	memset(buffer, 0, sizeof(buffer));
	memcpy(&buffer[0], signature, sig_len);
	comm->send(buffer, sig_len);
	printf("OK\n");
	
	// Wait for response
	printf("Receiving response...");
	memset(buffer, 0, sizeof(buffer));
	comm->receive(&buffer[0], 4096);
	printf("OK: %s\n", buffer);
}

void EXPORT_USERS()
{
	printf("Sending USER_EXPORT command...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "USER_EXPORT");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK.\n");

	// Expect data back
	printf("Receiving data...");
	memset(buffer, 0, sizeof(buffer));
	comm->receive(&buffer[0], 10000);
	printf("OK.\n");
}

// Send hash to be signed
void LOG_ADD()
{
	printf("Sending LOGS_ADD command...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "LOGS_ADD");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK.\n");

	// Now it expects:
	// DATA
	printf("Sending DATA...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "rm -rf inc/data/test/lol.txt");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK\n");

	// Wait for 'SUCCESS'
	printf("Receiving SUCCESS...");
	memset(buffer, 0, sizeof(buffer));
	comm->receive(&buffer[0], 4096);
	printf("OK: %s\n", buffer);

	// Wait for new data
	printf("Receiving new data...");
	uint8_t data[768];
	memset(data, 0, sizeof(data));
	len = comm->receive(&data[0], 768);
	printf("OK: %d\n", len);

	// Wait for signature
	printf("Receiving signature...");
	uint8_t signature[128];
	memset(signature, 0, sizeof(signature));
	len = comm->receive(&signature[0], 128);
	printf("OK: %d\n", len);
}

void START_SESSION()
{
	printf("Sending SESS_START command...");
	memset(buffer, 0, sizeof(buffer));
	sprintf_s((char*)buffer, sizeof(buffer), "SESS_START");
	comm->send(buffer, strlen((char*)buffer));
	printf("OK.\n");

	// Send CONNECTED
	comm->connect();

	// Send TIME_SEND and time
	TIME_SEND();

	// Initiate secure comm
	//return;

	int ret;
	mbedtls_ecdh_context ctx_cli, ctx_srv;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	unsigned char cli_to_srv[32], srv_to_cli[32];
	const char pers[] = "ecdh";

	mbedtls_aes_context aes_ctx;
	mbedtls_md_context_t sha_ctx;

	mbedtls_aes_init(&aes_ctx);
	mbedtls_md_init(&sha_ctx);

	unsigned char aes_output[128];
	memset(aes_output, 0, 128);

	mbedtls_ecdh_init(&ctx_cli);
	mbedtls_ecdh_init(&ctx_srv);
	mbedtls_ctr_drbg_init(&ctr_drbg);

	/*
	* Initialize random number generation
	*/
	mbedtls_entropy_init(&entropy);
	if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
		(const unsigned char *)pers,
		sizeof pers)) != 0)
	{
		mbedtls_printf(" failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret);
		return;
	}

	/*
	* Client: inialize context and generate keypair
	*/
	ret = mbedtls_ecp_group_load(&ctx_cli.grp, MBEDTLS_ECP_DP_CURVE25519);
	if (ret != 0)
	{
		mbedtls_printf(" failed\n  ! mbedtls_ecp_group_load returned %d\n", ret);
		return;
	}

	ret = mbedtls_ecdh_gen_public(&ctx_cli.grp, &ctx_cli.d, &ctx_cli.Q, mbedtls_ctr_drbg_random, &ctr_drbg);
	if (ret != 0)
	{
		mbedtls_printf(" failed\n  ! mbedtls_ecdh_gen_public returned %d\n", ret);
		return;
	}

	ret = mbedtls_mpi_write_binary(&ctx_cli.Q.X, cli_to_srv, 32);
	if (ret != 0)
	{
		mbedtls_printf(" failed\n  ! mbedtls_mpi_write_binary returned %d\n", ret);
		return;
	}

	// wait for "OK" because the device takes longer to do this than the computer
	comm->waitOK();

	// send 256-bits (32B) public key
	comm->send(cli_to_srv, 32);

	// get 256-bits (32B) public key
	comm->receive(&srv_to_cli[0], 32);

	/*
	* Client: read peer's key and generate shared secret
	*/
	ret = mbedtls_mpi_lset(&ctx_cli.Qp.Z, 1);
	if (ret != 0)
	{
		mbedtls_printf(" failed\n  ! mbedtls_mpi_lset returned %d\n", ret);
		return;
	}

	ret = mbedtls_mpi_read_binary(&ctx_cli.Qp.X, srv_to_cli, 32);
	if (ret != 0)
	{
		mbedtls_printf(" failed\n  ! mbedtls_mpi_read_binary returned %d\n", ret);
		return;
	}

	size_t len = 0;
	unsigned char buffer[BUFSIZE];

	if ((ret = mbedtls_ecdh_calc_secret(&ctx_cli,
		&len,
		buffer,
		BUFSIZE,
		mbedtls_ctr_drbg_random, &ctr_drbg)) != 0)
	{
		mbedtls_printf(" failed\n  ! mbedtls_ecdh_calc_secret returned %d\n", ret);
		return;
	}

	mbedtls_printf(" ok\n");

	printf("\n");

	printf("\n");
	mbedtls_printf("\nsecret: ");
	for (size_t i = 0; i < len; i++) {
		printf("%02x", buffer[i]);
	}

	unsigned char key[32] = { 0 };
	mbedtls_sha256(buffer, len, key, 0);

	printf("\n");
	mbedtls_printf("\nkey: ");
	for (int i = 0; i < 32; i++) {
		printf("%02x", key[i]);
	}

	printf("\n");

	// Set AES key
	comm->setKey(key);

	// Receive challenge
	uint8_t challenge[16];
	comm->receive(challenge, 16);

	printf("\n");
	mbedtls_printf("\nchallenge: ");
	for (int i = 0; i < 16; i++) {
		printf("%02x", challenge[i]);
	}

	printf("\n");

	///// Send modified challenge encrypted with session key

	uint8_t mod_challenge[16];
	for (int a = 0; a < 16; a++)
	{
		mod_challenge[a] = challenge[a] % 6; // plaintext[a] mod 6 for now...
	}

	comm->send(challenge, 16);

	mbedtls_ecdh_free(&ctx_srv);
	mbedtls_ecdh_free(&ctx_cli);
	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);
}

#endif