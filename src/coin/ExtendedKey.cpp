/* -*-c++-*- libcoin - Copyright (C) 2012 Michael Gronager
 *
 * libcoin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * libcoin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libcoin.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <coin/ExtendedKey.h>
#include <coin/Script.h>

#include <vector>

using namespace std;

// Generate a private key from just the secret parameter
static int EC_KEY_regenerate_key(EC_KEY *eckey, const BIGNUM *priv_key)
{
    int ok = 0;
    BN_CTX *ctx = NULL;
    EC_POINT *pub_key = NULL;
    
    if (!eckey) return 0;
    
    const EC_GROUP *group = EC_KEY_get0_group(eckey);
    
    if ((ctx = BN_CTX_new()) == NULL)
        goto err;
    
    pub_key = EC_POINT_new(group);
    
    if (pub_key == NULL)
        goto err;
    
    if (!EC_POINT_mul(group, pub_key, priv_key, NULL, NULL, ctx))
        goto err;
    
    EC_KEY_set_private_key(eckey,priv_key);
    EC_KEY_set_public_key(eckey,pub_key);
    
    ok = 1;
    
err:
    
    if (pub_key)
        EC_POINT_free(pub_key);
    if (ctx != NULL)
        BN_CTX_free(ctx);
    
    return(ok);
}

Point::Point(int curve) : _ec_point(NULL), _ec_group(EC_GROUP_new_by_curve_name(curve)) {
    _ec_point = EC_POINT_new(_ec_group);
}

Point::Point(const Point& point) : _ec_group(EC_GROUP_new_by_curve_name(NID_secp256k1)) {
    EC_GROUP_copy(_ec_group, point._ec_group);
    _ec_point = EC_POINT_new(_ec_group);
    EC_POINT_copy(_ec_point, point._ec_point);
}

Point::Point(const EC_POINT* p, int curve) : _ec_group(EC_GROUP_new_by_curve_name(curve)) {
    _ec_point = EC_POINT_new(_ec_group);
    EC_POINT_copy(_ec_point, p);
}

Point::Point(Infinity inf, int curve) : _ec_group(EC_GROUP_new_by_curve_name(curve)) {
    _ec_point = EC_POINT_new(_ec_group);
    EC_POINT_set_to_infinity(_ec_group, _ec_point);
}

Point::~Point() {
    EC_POINT_clear_free(_ec_point);
    EC_GROUP_clear_free(_ec_group);
}

Point& Point::operator=(const Point& point) {
    _ec_group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    EC_GROUP_copy(_ec_group, point._ec_group);
    _ec_point = EC_POINT_new(_ec_group);
    EC_POINT_copy(_ec_point, point._ec_point);
    
    return *this;
}

CBigNum Point::X() const {
    CBigNum x;
    CBigNum y;
    EC_POINT_get_affine_coordinates_GFp(_ec_group, _ec_point, &x, &y, NULL);
    return x;
}

Point& Point::operator+=(const Point& point) {
    EC_POINT* r = EC_POINT_new(_ec_group);
    EC_POINT_add(_ec_group, r, _ec_point, point._ec_point, NULL);
    EC_POINT_clear_free(_ec_point);
    _ec_point = r;
    return *this;
}

EC_GROUP* Point::ec_group() const {
    return _ec_group;
}

EC_POINT* Point::ec_point() const {
    return _ec_point;
}

Point operator*(const CBigNum& m, const Point& Q) {
    if (!EC_POINT_is_on_curve(Q.ec_group(), Q.ec_point(), NULL))
        throw std::runtime_error("Q not on curve");
    
    Point R(Q);
    
    if (!EC_POINT_mul(R.ec_group(), R.ec_point(), NULL, Q.ec_point(), &m, NULL))
        throw std::runtime_error("Multiplication error");
    
    return R;
}



Key::Key() : _ec_key(NULL) {
    _ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    EC_KEY_set_conv_form(_ec_key, POINT_CONVERSION_COMPRESSED);
}

Key::Key(const CBigNum& private_number) : _ec_key(NULL) {
    _ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    EC_KEY_regenerate_key(_ec_key, &private_number);
    EC_KEY_set_conv_form(_ec_key, POINT_CONVERSION_COMPRESSED);
}

Key::Key(const Point& public_point) : _ec_key(NULL) {
    _ec_key = EC_KEY_new();
    EC_KEY_set_group(_ec_key, public_point.ec_group());
    EC_KEY_set_public_key(_ec_key, public_point.ec_point());
    EC_KEY_set_conv_form(_ec_key, POINT_CONVERSION_COMPRESSED);
}

bool Key::isPrivate() const {
    return EC_KEY_get0_private_key(_ec_key);
}

void Key::reset() {
    EC_KEY_generate_key(_ec_key);
    EC_KEY_set_conv_form(_ec_key, POINT_CONVERSION_COMPRESSED);
}

void Key::reset(const CBigNum& private_number) {
    EC_KEY_regenerate_key(_ec_key, &private_number);
    EC_KEY_set_conv_form(_ec_key, POINT_CONVERSION_COMPRESSED);
}

void Key::reset(const Point& public_point) {
    EC_KEY_set_group(_ec_key, public_point.ec_group());
    EC_KEY_set_conv_form(_ec_key, POINT_CONVERSION_COMPRESSED);
    EC_KEY_set_public_key(_ec_key, public_point.ec_point());
}

Data Key::serialized_pubkey() const {
    Data data(33, 0); // has to be 33 bytes long!
    unsigned char* begin = &data[0];
    if (i2o_ECPublicKey(_ec_key, &begin) != 33)
        throw std::runtime_error("i2o_ECPublicKey returned unexpected size, expected 33");
    
    return data;
}

Data Key::serialized_full_pubkey() const {
    Data data(65, 0); // has to be 65 bytes long!
    unsigned char* begin = &data[0];
    EC_KEY* full_ec = EC_KEY_new();
    EC_KEY_set_conv_form(_ec_key, POINT_CONVERSION_UNCOMPRESSED);
    EC_KEY_copy(full_ec, _ec_key);
    if (i2o_ECPublicKey(_ec_key, &begin) != 65)
        throw std::runtime_error("i2o_ECPublicKey returned unexpected size, expected 65");
    
    return data;
}

SecureData Key::serialized_privkey() const {
    if (!isPrivate())
        throw std::runtime_error("cannot serialize priv key in neutered key");
    SecureData data(32, 0); // has to be 32 bytes long!
    const BIGNUM *bn = EC_KEY_get0_private_key(_ec_key);
    int bytes = BN_num_bytes(bn);
    if (bn == NULL)
        throw std::runtime_error("EC_KEY_get0_private_key failed");
    if ( BN_bn2bin(bn, &data[32 - bytes]) != bytes)
        throw std::runtime_error("BN_bn2bin failed");
    
    return data;
}

const Point Key::public_point() const {
    return Point(EC_KEY_get0_public_key(_ec_key));
}

CBigNum Key::order() const {
    CBigNum bn;
    EC_GROUP_get_order(EC_KEY_get0_group(_ec_key) , &bn, NULL);
    return bn;
}

CBigNum Key::number() const {
    CBigNum bn;
    return CBigNum(EC_KEY_get0_private_key(_ec_key));
}

const BIGNUM* Key::private_number() const {
    return EC_KEY_get0_private_key(_ec_key);
}

class COIN_EXPORT HMAC {
public:
    enum Digest {
        MD5,
        SHA1,
        SHA256,
        SHA512,
        RIPEMD160
    };
    
    HMAC(Digest digest = SHA512) : _digest(digest) {
    }
    
    template <typename DatA, typename DatB>
    SecureData operator()(const DatA& key, const DatB& message) const {
        const EVP_MD* md = NULL;
        switch (_digest) {
            case MD5 :
                md = EVP_md5();
                break;
            case SHA1 :
                md = EVP_sha1();
                break;
            case SHA256 :
                md = EVP_sha256();
                break;
            case SHA512 :
                md = EVP_sha512();
                break;
            case RIPEMD160 :
                md = EVP_ripemd160();
                break;
            default : md = NULL;
                break;
        }
        SecureData mac(EVP_MAX_MD_SIZE);
        unsigned int size;
        ::HMAC(md, &key[0], key.size(), &message[0], message.size(), &mac[0], &size);
        mac.resize(size);
        return mac;
    }
private:
    Digest _digest;
};

ExtendedKey::ExtendedKey(SecureData seed) : Key() {
    if (seed.size() == 0) {
        seed.resize(256/8);
        RAND_bytes(&seed[0], 256/8);
    }
    
    class HMAC hmac(HMAC::SHA512);
    char* bitcoin_seed = "Bitcoin seed";
    SecureData key(bitcoin_seed, &bitcoin_seed[12]);
    SecureData I = hmac(key, seed);
    
    SecureData I_L(I.begin(), I.begin() + 256/8);
    SecureData I_R(I.begin() + 256/8, I.end());
    
    BIGNUM* bn = BN_bin2bn(&I_L[0], 32, NULL);
    CBigNum k(bn);
    CBigNum n = order();
    
    reset(k % n);
    _chain_code = I_R;
}

ExtendedKey::ExtendedKey(const CBigNum& private_number, const SecureData& chain_code) : Key(private_number), _chain_code(chain_code) {
}

ExtendedKey::ExtendedKey(const Point& public_point, const SecureData& chain_code) : Key(public_point), _chain_code(chain_code) {
}

const SecureData& ExtendedKey::chain_code() const {
    return _chain_code;
}

unsigned int ExtendedKey::hash() const {
    Data pubkey = serialized_pubkey();
    uint256 hash1;
    SHA256(&pubkey[0], pubkey.size(), (unsigned char*)&hash1);
    uint160 md;
    RIPEMD160((unsigned char*)&hash1, sizeof(hash1), (unsigned char*)&md);
    unsigned char *p = (unsigned char*)&md;
    unsigned int fp = 0;
    fp |= p[0] << 24;
    fp |= p[1] << 16;
    fp |= p[2] << 8;
    fp |= p[3];
    return fp;
}

/// fingerprint of the chain and the public key - this is a more precise identification (less collisions) than the 4byte fingerprint.
uint160 ExtendedKey::fingerprint() const {
    SecureData data(_chain_code);
    Data pub = serialized_pubkey();
    data.insert(data.end(), pub.begin(), pub.end());
    uint256 hash;
    SHA256(&data[0], data.size(), (unsigned char*)&hash);
    uint160 md;
    RIPEMD160((unsigned char*)&hash, sizeof(hash), (unsigned char*)&md);
    return md;
}

// derive a new extended key
ExtendedKey ExtendedKey::derive(unsigned int i, bool multiply) const {
    if (i & 0x80000000) // BIP0032 compatability
        return delegate(i & 0x7fffffff);
    
    // K concat i
    Data data = serialized_pubkey();
    unsigned char* pi = (unsigned char*)&i;
    data.push_back(pi[3]);
    data.push_back(pi[2]);
    data.push_back(pi[1]);
    data.push_back(pi[0]);
    
    class HMAC hmac(HMAC::SHA512);
    SecureData I = hmac(_chain_code, data);
    
    SecureData I_L(I.begin(), I.begin() + 256/8);
    SecureData I_R(I.begin() + 256/8, I.end());
    
    if (isPrivate()) {
        CBigNum k(private_number());
        BIGNUM* bn = BN_bin2bn(&I_L[0], 32, NULL);
        CBigNum k_i;
        if (multiply)
            k_i = k * CBigNum(bn);
        else
            k_i = k + CBigNum(bn);
        CBigNum n = order();
        //            n.SetHex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");
        return ExtendedKey(k_i % n, I_R);
    }
    
    // calculate I_L*G - analogue to create a key with the secret I_L and get its EC_POINT pubkey, then add it to this EC_POINT pubkey;
    BIGNUM* bn = BN_bin2bn(&I_L[0], 32, NULL);
    if (multiply) {
        Point mult = CBigNum(bn) * public_point();
        return ExtendedKey(mult, I_R);
    }
    else {
        Key d((CBigNum(bn)));
        Point sum = public_point() + d.public_point();
        return ExtendedKey(sum, I_R);
    }
}

// delegate to get a new isolated private key hieracy
ExtendedKey ExtendedKey::delegate(unsigned int i) const {
    i |= 0x80000000;
    // 0x0000000 concat k concat i
    SecureData data = serialized_privkey();
    data.insert(data.begin(), 1, 0x00);
    unsigned char* pi = (unsigned char*)&i;
    data.push_back(pi[3]);
    data.push_back(pi[2]);
    data.push_back(pi[1]);
    data.push_back(pi[0]);
    
    class HMAC hmac(HMAC::SHA512);
    SecureData I = hmac(_chain_code, data);
    
    SecureData I_L(I.begin(), I.begin() + 256/8);
    SecureData I_R(I.begin() + 256/8, I.end());
    
    if (isPrivate()) {
        CBigNum k(private_number());
        BIGNUM* bn = BN_bin2bn(&I_L[0], 32, NULL);
        CBigNum k_i = k + CBigNum(bn);
        CBigNum n = order();
        //            n.SetHex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");
        return ExtendedKey(k_i % n, I_R);
    }
    throw "Cannot delegate a public key";
}

ExtendedKey::Derivatives ExtendedKey::parse(const std::string tree) const {
    // first skip all non number stuff - it is nice to be able to keep it there to e.g. write m/0'/1 - i.e. keep an m
    Derivatives derivatives;
    std::string::const_iterator c = tree.begin();
    // skip anything that is not a digit or '
    while (c != tree.end() && !isdigit(*c)) ++c;
    while (c != tree.end()) {
        std::string d;
        while (c != tree.end() && isdigit(*c)) {
            d.push_back(*c);
            ++c;
        }
        if (d.size()) {
            unsigned int i = boost::lexical_cast<unsigned int>(d);
            if (c != tree.end() && *c == '\'') {
                i |= 0x80000000;
                ++c;
            }
            derivatives.push_back(i);
        }
        while (c != tree.end() && !isdigit(*c)) ++c; // skip '/'
    }
    return derivatives;
}

ExtendedKey ExtendedKey::path(const std::string tree, unsigned char& depth, unsigned int& hash, unsigned int& child_number) const {
    // num(delegate=')/num(delegate=')/...
    Derivatives derivatives = parse(tree);
    ExtendedKey ek(*this);
    hash = 0;
    depth = 0;
    child_number = 0;
    for (Derivatives::const_iterator d = derivatives.begin(); d != derivatives.end(); ++d) {
        child_number = *d;
        hash = ek.hash();
        depth++;
        ek = ek.derive(child_number);
    }
    
    return ek;
}

ExtendedKey ExtendedKey::path(const std::string tree) const {
    unsigned char depth;
    unsigned int hash;
    unsigned int child_number;
    return path(tree, depth, hash, child_number);
}

CKey ExtendedKey::key() const {
    if (isPrivate()) {
        SecureData prv = serialized_privkey();
        CKey key;
        key.SetSecret(prv, true); // we use compressed public keys!
        return key;
    }
    else {
        Data pub = serialized_pubkey();
        CKey key;
        key.SetPubKey(pub);
        return key;
    }
}

Data ExtendedKey::serialize(bool serialize_private, unsigned int version, unsigned char depth, unsigned int hash, unsigned int child_number) const {
    Data data;
    if (version > 0) {
        unsigned char* p = (unsigned char*)&version;
        data.push_back(p[3]);
        data.push_back(p[2]);
        data.push_back(p[1]);
        data.push_back(p[0]);
        data.push_back(depth);
        p = (unsigned char*)&hash;
        data.push_back(p[3]);
        data.push_back(p[2]);
        data.push_back(p[1]);
        data.push_back(p[0]);
        p = (unsigned char*)&child_number;
        data.push_back(p[3]);
        data.push_back(p[2]);
        data.push_back(p[1]);
        data.push_back(p[0]);
    }
    data.insert(data.end(), _chain_code.begin(), _chain_code.end());
    if (serialize_private) {
        SecureData priv = serialized_privkey();
        data.push_back(0);
        data.insert(data.end(), priv.begin(), priv.end());
    }
    else {
        Data pub = serialized_pubkey();
        data.insert(data.end(), pub.begin(), pub.end());
    }
    return data;
}



CKey ExtendedKey::operator()(const ExtendedKey::Generator& generator) const {
    CKey key;
    
    // check that the generator fingerprint matches ours:
    if (generator.fingerprint() != fingerprint())
        throw runtime_error("Request for derivative of another extended key!");

    // now run through the derivatives:
    ExtendedKey ek(*this);
    size_t depth = 0;
    for (Derivatives::const_iterator d = generator.derivatives().begin(); d != generator.derivatives().end(); ++d) {
        unsigned int child_number = *d;
        depth++;
        ek = ek.derive(child_number, true); // note ! the generator uses multiply instead of add!
    }
    
    // now extract the Key from the ExtendedKey
    
    return ek.key();
}

ExtendedKey::Generator::Generator(std::vector<unsigned char> script_data) {
    Script script(script_data.begin(), script_data.end());

    Evaluator eval;
    eval(script);
    Evaluator::Stack stack = eval.stack();
    
    if (stack.size() < 2)
        throw runtime_error("Generator - need at least one derivative to make a Key");

    if (stack[0].size() != 20)
        throw runtime_error("Generator - expects a script starting with a fingerprint");

    _fingerprint = uint160(stack[0]);
    
    for (size_t i = 1; i < stack.size(); ++i) {
        CBigNum bn(stack[i]);
        unsigned int n = bn.getuint();
        _derivatives.push_back(n);
    }
}

ExtendedKey::Generator& ExtendedKey::Generator::operator++() {
    // first check that the last path is not a delegation (generators only work through derivation)
    if (_derivatives.empty() || 0x80000000 & _derivatives.back() || _derivatives.back() == 0x7fffffff )
        _derivatives.push_back(0);
    else
        _derivatives.back()++;
    
    return *this;
}

std::vector<unsigned char> ExtendedKey::Generator::serialize() const {
    Script script;
    script << _fingerprint;
    for (Derivatives::const_iterator n = _derivatives.begin(); n != _derivatives.end(); ++n)
        script << *n;
    
    return script;
}

boost::tribool ExtendedKeyEvaluator::eval(opcodetype opcode) {
    boost::tribool result = TransactionEvaluator::eval(opcode);
    if (result || !result)
        return result;
    switch (opcode) {
        case OP_RESOLVE: {
            // resolve the key - assuming that the stack contains a generator
            if (_stack.size() < 1)
                return false;
            
            ExtendedKey::Generator generator(top(-1));
            
            CKey key = _exkey(generator);
            
            pop(_stack);
            
            PubKey pk = key.GetPubKey();
            
            _stack.push_back(pk);
            
            break;
        }
        case OP_RESOLVEANDSIGN: {
            // resolve the key - assuming that the stack contains a generator, and a serialized script
            if (_stack.size() < 2)
                return false;
            
            ExtendedKey::Generator generator(top(-1));
            
            CKey key = _exkey(generator);
            
            Script script(top(-2));
            
            uint256 hash = _txn.getSignatureHash(script, _in, _hash_type);
            
            Value signature;
            if (!key.Sign(hash, signature))
                return false;
            
            pop(_stack);
            pop(_stack);
            
            signature.push_back(_hash_type);
            
            _stack.push_back(signature);
            
            break;
        }
        default:
            break;
    }
    return boost::indeterminate;
}

