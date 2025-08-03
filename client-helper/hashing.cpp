
#include <openssl/sha.h>
#include <string>
#include <vector>
using namespace std;

string ConvertToHex(unsigned char c) {
  string hex;
  char chars[3]; // A char array of size 3 to hold 2 hexadecimal digits and a
                 // null-terminator
  snprintf(chars, sizeof(chars), "%02x",
           c); // snprintf prints the hexadecimal representation of c into chars
  hex.append(
      chars); // Appends the converted hexadecimal characters to the string hex
  return hex;
}

string calculateSHA1Hash(unsigned char *buffer, int size) {
  unsigned char hash[SHA_DIGEST_LENGTH]; // This array of size 20 bytes will
                                         // hold the SHA-1 hash

  // SHA1 calls the OpenSSL SHA1 function to calculate the hash of buffer of
  // length size, and store it in hash
  SHA1(buffer, size, hash);

  // SHA1() expects the data to be hashed to be of type const unsigned char*
  // it produces a hash in the form of a byte array, where each byte is an
  // unsigned 8-bit integer like 10010011 as it is binary data we convert to to
  // hwxadecimal representation as each byte (8 bits) can be represented by
  // exactly two hexadecimal digits

  // this will hold the final hexadecimal representation of the hash
  string hashOfChunk;
  for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
    hashOfChunk += ConvertToHex(hash[i]);
  }

  return hashOfChunk;
}

vector<unsigned char> hexStringToByteArray(string &hexString) {
  vector<unsigned char> byteArray;

  for (int i = 0; i < hexString.length(); i += 2) {
    string byteString = hexString.substr(i, 2);
    char byte = (char)strtol(byteString.c_str(), NULL, 16);
    byteArray.push_back(byte);
  }

  return byteArray;
}
