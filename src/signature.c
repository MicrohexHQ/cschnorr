#include "signature.h"

#include <string.h>

#include <openssl/obj_mac.h>
#include <openssl/sha.h>

int hash(unsigned char* out, const unsigned char* in, const size_t len) {
    SHA256_CTX sha256CTX;

    if(!SHA256_Init(&sha256CTX)) {
        return 0;
    }

    if(!SHA256_Update(&sha256CTX, in, len)) {
        return 0;
    }

    if(!SHA256_Final(out, &sha256CTX)) {
        return 0;
    }

    return SHA256_DIGEST_LENGTH;
}

int gen_r(unsigned char* r,
          BIGNUM* k) {
    EC_GROUP* group = NULL;
    BN_CTX* ctx = NULL;
    EC_POINT* R = NULL;
    BIGNUM* Rx = NULL;
    BIGNUM* Ry = NULL;
    BIGNUM* tmp = NULL;
    int error = 1;
    
    group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if(group == NULL) {
        goto cleanup;
    }

    ctx = BN_CTX_new();
    if(ctx == NULL) {
        goto cleanup;
    }

    const EC_POINT* G = EC_GROUP_get0_generator(group);
    if(G == NULL) {
        goto cleanup;
    }

    R = EC_POINT_new(group);
    if(R == NULL) {
        goto cleanup;
    }

    if(EC_POINT_mul(group, R, NULL, G, k, ctx) == 0) {
        goto cleanup;
    }

    Rx = BN_new();
    if(Rx == NULL) {
        goto cleanup;
    }
    
    Ry = BN_new();
    if(Ry == NULL) {
        goto cleanup;
    }

    if(EC_POINT_get_affine_coordinates_GFp(group, R, Rx, Ry, ctx) == 0) {
        goto cleanup;
    }

    if(BN_is_odd(Ry)) {
        tmp = BN_new();
        if(tmp == NULL) {
            goto cleanup;
        }
        BN_zero(tmp);
        if(BN_sub(k, tmp, k) == 0) {
            goto cleanup;
        }

        if(EC_POINT_mul(group, R, NULL, G, k, ctx) == 0) {
            goto cleanup;
        }

        if(EC_POINT_get_affine_coordinates_GFp(group, R, Rx, Ry, ctx) == 0) {
            goto cleanup;
        }
    }

    if(BN_bn2bin(Rx, r) <= 0) {
        goto cleanup;
    }

    error = 0;

    cleanup:
    BN_free(Rx);
    BN_free(Ry);
    EC_POINT_free(R);
    BN_free(tmp);
    BN_CTX_free(ctx);
    EC_GROUP_free(group);
    if(error) {
        return 0;
    }

    return 1;
}

int schnorr_sign(schnorr_sig** dest, 
                 const schnorr_key* key, 
                 const unsigned char* msg, 
                 const size_t len) {
    EC_GROUP* group = NULL;
    BIGNUM* k = NULL;
    BN_CTX* ctx = NULL;
    EC_POINT* R = NULL;
    BIGNUM* BNh = NULL;
    BIGNUM* s = NULL;
    int error = 1;

    *dest = malloc(sizeof(schnorr_sig));
    if(*dest == NULL) {
        goto cleanup;
    }
    (*dest)->s = NULL;

    group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if(group == NULL) {
        goto cleanup;
    }

    k = BN_new();
    if(k == NULL) {
        goto cleanup;
    }

    if(BN_rand(k, 256, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY) != 1) {
        goto cleanup;
    }

    if(gen_r((*dest)->r, k) == 0) {
        goto cleanup;
    }

    BNh = BN_new();
    if(BNh == NULL) {
        goto cleanup;
    }

    if(gen_h(msg, len, (*dest)->r, BNh) == 0) {
        goto cleanup;
    }

    ctx = BN_CTX_new();
    if(ctx == NULL) {
        goto cleanup;
    }
    
    s = BN_new();
    if(s == NULL) {
        goto cleanup;
    }

    if(BN_mul(s, BNh, key->a, ctx) == 0) {
        goto cleanup;
    }

    if(BN_sub(s, k, s) == 0) {
        goto cleanup;
    }

    (*dest)->s = s;

    error = 0;

    cleanup:
    EC_GROUP_free(group);
    BN_free(BNh);
    BN_CTX_free(ctx);
    BN_free(k);
    if(error) {
        if(*dest != NULL) {
            BN_free((*dest)->s);
        }

        free(*dest);

        return 0;
    }

    return 1;
}

void schnorr_sig_free(schnorr_sig* sig) {
    BN_free(sig->s);
    free(sig);
}

int schnorr_verify(const schnorr_sig* sig,
                   const schnorr_pubkey* pubkey,
                   const unsigned char* msg,
                   const size_t len) {
    EC_GROUP* group = NULL;
    BN_CTX* ctx = NULL;
    BIGNUM* order = NULL;
    BIGNUM* BNh = NULL;
    EC_POINT* R = NULL;
    BIGNUM* Rx = NULL;
    BIGNUM* Ry = NULL; 
    int retval = 0;

    group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if(group == NULL) {
        goto cleanup;
    }

    ctx = BN_CTX_new();
    if(ctx == NULL) {
        goto cleanup;
    }

    order = BN_new();
    if(order == NULL) {
        goto cleanup;
    }

    if(EC_GROUP_get_order(group, order, ctx) == 0) {
        goto cleanup;
    }

    if(BN_cmp(sig->s, order) != -1) {
        retval = -1;
        goto cleanup;
    }

    BNh = BN_new();
    if(BNh == NULL) {
        goto cleanup;
    }

    const int genRes = gen_h(msg, len, sig->r, BNh);
    if(genRes != 1) {
        retval = genRes;
        goto cleanup;
    }

    R = EC_POINT_new(group);
    if(R == NULL) {
        goto cleanup;
    }

    if(EC_POINT_mul(group, R, sig->s, pubkey->A, BNh, ctx) == 0) {
        goto cleanup;
    }

    Rx = BN_new();
    if(Rx == NULL) {
        goto cleanup;
    }
    
    Ry = BN_new();
    if(Ry == NULL) {
        goto cleanup;
    }

    if(EC_POINT_get_affine_coordinates_GFp(group, R, Rx, Ry, ctx) == 0) {
        goto cleanup;
    }

    if(BN_is_odd(Ry)) {
        retval = -1;
        goto cleanup;
    }

    if(EC_POINT_is_at_infinity(group, R) == 1) {
        retval = -1;
        goto cleanup;
    }

    unsigned char x[32];
    if(BN_bn2bin(Rx, (unsigned char*)&x) <= 0) {
        goto cleanup;
    }

    const int ret = memcmp(x, sig->r, 32);

    retval = 1;

    cleanup:
    EC_GROUP_free(group);
    EC_POINT_free(R);
    BN_CTX_free(ctx);
    BN_free(Rx);
    BN_free(Ry);
    BN_free(BNh);
    BN_free(order);

    if(retval != 1) {
        return retval;
    }

    if(ret == 0) {
        return 1;
    } else {
        return -1;
    }
}

int gen_h(const unsigned char* msg, const size_t len, const unsigned char* r, BIGNUM* out) {
    BN_CTX* ctx = NULL;
    BIGNUM* order = NULL;
    EC_GROUP* group = NULL;
    int error = 0;
    
    unsigned char msgHash[32];
    if(hash((unsigned char*)&msgHash, msg, len) == 0) {
        goto cleanup;
    }

    unsigned char payload[64];
    memcpy(&payload, r, 32);
    memcpy(((unsigned char*)&payload) + 32, msgHash, 32);

    unsigned char h[32];
    if(hash((unsigned char*)&h, payload, 64) == 0) {
        goto cleanup;
    }
   
    if(BN_bin2bn((unsigned char*)&h, 32, out) == NULL) {
        goto cleanup;
    }

    if(BN_is_zero(out) == 1) {
        error = -1;
        goto cleanup;
    }

    order = BN_new();
    if(order == NULL) {
        goto cleanup;
    }

    ctx = BN_CTX_new();
    if(ctx == NULL) {
        goto cleanup;
    }

    group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if(group == NULL) {
        goto cleanup;
    }

    if(EC_GROUP_get_order(group, order, ctx) == 0) {
        goto cleanup;
    }
    
    if(BN_cmp(out, order) != -1) {
        error = -1;
        goto cleanup;
    }

    error = 1;

    cleanup:
    EC_GROUP_free(group);
    BN_CTX_free(ctx);
    BN_free(order);

    return error;
}

int committed_r_sign(committed_r_sig** dest,
                     const committed_r_key* key,
                     const unsigned char* msg,
                     const size_t len) {
    BIGNUM* BNh = NULL;
    BN_CTX* ctx = NULL;
    int error = 1;
    
    *dest = malloc(sizeof(committed_r_sig));
    if(*dest == NULL) {
        goto cleanup;
    }
    (*dest)->s = NULL;

    BNh = BN_new();
    if(BNh == NULL) {
        goto cleanup;
    }

    if(gen_h(msg, len, key->pub->r, BNh) == 0) {
        goto cleanup;
    }

    ctx = BN_CTX_new();
    if(ctx == NULL) {
        goto cleanup;
    }

    (*dest)->s = BN_new();
    if((*dest)->s == NULL) {
        goto cleanup;
    }

    if(BN_mul((*dest)->s, BNh, key->a, ctx) == 0) {
        goto cleanup;
    }

    if(BN_sub((*dest)->s, key->k, (*dest)->s) == 0) {
        goto cleanup;
    }

    error = 0;

    cleanup:
    BN_free(BNh);
    BN_CTX_free(ctx);
    if(error) {
        if(*dest != NULL) {
            BN_free((*dest)->s);
        }
        free(*dest);
        return 0;
    }

    return 1;
}

int committed_r_verify(const committed_r_sig* sig,
                       const committed_r_pubkey* pubkey,
                       const unsigned char* msg,
                       const size_t len) {
    schnorr_sig* sSig = NULL;
    schnorr_pubkey* pKey = NULL;
    int retval = 0;

    sSig = malloc(sizeof(schnorr_sig));
    if(sSig == NULL) {
        goto cleanup;
    }

    memcpy(sSig->r, pubkey->r, 32);
    sSig->s = sig->s;

    pKey = malloc(sizeof(schnorr_pubkey));
    if(pKey == NULL) {
        goto cleanup;
    }
    
    pKey->A = pubkey->A;

    retval = schnorr_verify(sSig, pKey, msg, len);

    cleanup:
    free(sSig);
    free(pKey);

    return retval;
}

void committed_r_sig_free(committed_r_sig* sig) {
    BN_free(sig->s);
    free(sig);
}

int committed_r_recover(const committed_r_sig* sig1,
                        const unsigned char* msg1,
                        const size_t len1,
                        const committed_r_sig* sig2,
                        const unsigned char* msg2,
                        const size_t len2,
                        const committed_r_pubkey* pubkey,
                        committed_r_key** dest) {
    BIGNUM* h1 = NULL;
    BIGNUM* h2 = NULL;
    BN_CTX* ctx = NULL;
    EC_GROUP* group = NULL;
    int retval = 0;

    *dest = malloc(sizeof(committed_r_key));
    if(*dest == NULL) {
        goto cleanup;
    }
    (*dest)->a = NULL;
    (*dest)->k = NULL;
    (*dest)->pub = NULL;

    (*dest)->a = BN_new();
    if((*dest)->a == NULL) {
        goto cleanup;
    }

    (*dest)->k = BN_new();
    if((*dest)->k == NULL) {
        goto cleanup;
    }

    (*dest)->pub = malloc(sizeof(committed_r_pubkey));
    if((*dest)->pub == NULL) {
        goto cleanup;
    }
    (*dest)->pub->A = NULL;

    group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if(group == NULL) {
        goto cleanup;
    }

    (*dest)->pub->A = EC_POINT_new(group);
    if((*dest)->pub->A == NULL) {
        goto cleanup;
    }

    if(BN_sub((*dest)->a, sig2->s, sig1->s) == 0) {
        goto cleanup;
    }

    h1 = BN_new();
    if(h1 == NULL) {
        goto cleanup;
    }

    int genRes = gen_h(msg1, len1, pubkey->r, h1);
    if(genRes != 1) {
        retval = genRes;
        goto cleanup;
    }

    h2 = BN_new();
    if(h2 == NULL) {
        goto cleanup;
    }

    genRes = gen_h(msg2, len2, pubkey->r, h2);
    if(genRes != 1) {
        retval = genRes;
        goto cleanup;
    }

    if(BN_sub(h1, h1, h2) == 0) {
        goto cleanup;
    }

    ctx = BN_CTX_new();
    if(ctx == NULL) {
        goto cleanup;
    }

    if(BN_div((*dest)->a, NULL, (*dest)->a, h1, ctx) == 0) {
        goto cleanup;
    }

    if(BN_mul((*dest)->k, h2, (*dest)->a, ctx) == 0) {
        goto cleanup;
    }

    if(BN_add((*dest)->k, sig2->s, (*dest)->k) == 0) {
        goto cleanup;
    }

    if(gen_r((*dest)->pub->r, (*dest)->k) == 0) {
        goto cleanup;
    }

    const EC_POINT* G = EC_GROUP_get0_generator(group);
    if(G == NULL) {
        goto cleanup;
    }

    if(EC_POINT_mul(group, (*dest)->pub->A, NULL, G, (*dest)->a, ctx) == 0) {
        goto cleanup;
    }

    retval = 1;

    cleanup:
    BN_CTX_free(ctx);
    BN_free(h1);
    BN_free(h2);
    EC_GROUP_free(group);
    if(retval != 1) {
        if(*dest != NULL) {
            BN_free((*dest)->a);
            BN_free((*dest)->k);
            if((*dest)->pub != NULL) {
                EC_POINT_free((*dest)->pub->A);
                free((*dest)->pub);
            }
            free(*dest);
        }

        return 0;
    }

    return 1;
}