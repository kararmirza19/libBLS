/*
  Copyright (C) 2018-2019 SKALE Labs

  This file is part of libBLS.

  libBLS is free software: you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  libBLS is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with libBLS. If not, see <https://www.gnu.org/licenses/>.

  @file threshold_encryption.cpp
  @author Oleh Nikolaiev
  @date 2019
*/

#include <string.h>
#include <iostream>
#include <valarray>

#include "../tools/utils.h"
#include <threshold_encryption.h>
#include <threshold_encryption/utils.h>

namespace encryption {

TE::TE( const size_t t, const size_t n ) : t_( t ), n_( n ) {}

TE::~TE() {}

std::string TE::Hash(
    const libff::alt_bn128_G2& Y, std::string ( *hash_func )( const std::string& str ) ) {
    auto vectorCoordinates = G2ToString( Y );

    std::string tmp = "";
    for ( const auto& coord : vectorCoordinates ) {
        tmp += coord;
    }

    const std::string sha256hex = hash_func( tmp );

    return sha256hex;
}

libff::alt_bn128_G1 TE::HashToGroup( const libff::alt_bn128_G2& U, const std::string& V,
    std::string ( *hash_func )( const std::string& str ) ) {
    // assumed that U lies in G1

    auto U_str = G2ToString( U );

    const std::string sha256hex = hash_func( U_str[0] + U_str[1] + U_str[2] + U_str[3] + V );

    auto hash_bytes_arr = std::make_shared< std::array< uint8_t, 32 > >();
    std::string hash_str = cryptlite::sha256::hash_hex( sha256hex );
    for ( size_t i = 0; i < 32; ++i ) {
        hash_bytes_arr->at( i ) = static_cast< uint8_t >( hash_str[i] );
    }

    return HashtoG1( hash_bytes_arr );
}

Ciphertext TE::Encrypt( const std::string& message, const libff::alt_bn128_G2& common_public ) {
    libff::alt_bn128_Fr r = libff::alt_bn128_Fr::random_element();

    while ( r.is_zero() ) {
        r = libff::alt_bn128_Fr::random_element();
    }

    libff::alt_bn128_G2 U, Y;
    U = r * libff::alt_bn128_G2::one();
    Y = r * common_public;

    std::string hash = this->Hash( Y );

    // assuming message and hash are the same size strings
    // the behaviour is undefined when the two arguments are valarrays with different sizes

    std::valarray< uint8_t > lhs_to_hash( hash.size() );
    for ( size_t i = 0; i < hash.size(); ++i ) {
        lhs_to_hash[i] = static_cast< uint8_t >( hash[i] );
    }

    std::valarray< uint8_t > rhs_to_hash( message.size() );
    for ( size_t i = 0; i < message.size(); ++i ) {
        rhs_to_hash[i] = static_cast< uint8_t >( message[i] );
    }


    std::valarray< uint8_t > res = lhs_to_hash ^ rhs_to_hash;

    std::string V = "";
    for ( size_t i = 0; i < res.size(); ++i ) {
        V += static_cast< char >( res[i] );
    }

    libff::alt_bn128_G1 W, H;

    H = this->HashToGroup( U, V );
    W = r * H;

    Ciphertext result;
    std::get< 0 >( result ) = U;
    std::get< 1 >( result ) = V;
    std::get< 2 >( result ) = W;

    return result;
}

libff::alt_bn128_G2 TE::getDecryptionShare(
    const Ciphertext& ciphertext, const libff::alt_bn128_Fr& secret_key ) {
    checkCypher( ciphertext );
    if ( secret_key.is_zero() )
        throw std::runtime_error( "zero secret key" );

    libff::alt_bn128_G2 U = std::get< 0 >( ciphertext );

    std::string V = std::get< 1 >( ciphertext );

    libff::alt_bn128_G1 W = std::get< 2 >( ciphertext );

    libff::alt_bn128_G1 H = this->HashToGroup( U, V );

    libff::alt_bn128_GT fst, snd;
    fst = libff::alt_bn128_ate_reduced_pairing( W, libff::alt_bn128_G2::one() );
    snd = libff::alt_bn128_ate_reduced_pairing( H, U );

    bool res = fst == snd;

    if ( !res ) {
        throw std::runtime_error( "cannot decrypt data" );
    }

    libff::alt_bn128_G2 ret_val = secret_key * U;

    return ret_val;
}

bool TE::Verify( const Ciphertext& ciphertext, const libff::alt_bn128_G2& decryptionShare,
    const libff::alt_bn128_G2& public_key ) {
    libff::alt_bn128_G2 U = std::get< 0 >( ciphertext );

    std::string V = std::get< 1 >( ciphertext );

    libff::alt_bn128_G1 W = std::get< 2 >( ciphertext );

    libff::alt_bn128_G1 H = this->HashToGroup( U, V );

    libff::alt_bn128_GT fst, snd;
    fst = libff::alt_bn128_ate_reduced_pairing( W, libff::alt_bn128_G2::one() );
    snd = libff::alt_bn128_ate_reduced_pairing( H, U );

    bool res = fst == snd;

    bool ret_val = true;

    if ( res ) {
        if ( decryptionShare.is_zero() ) {
            ret_val = false;
        } else {
            libff::alt_bn128_GT pp1, pp2;
            pp1 = libff::alt_bn128_ate_reduced_pairing( W, public_key );
            pp2 = libff::alt_bn128_ate_reduced_pairing( H, decryptionShare );

            bool check = pp1 == pp2;
            if ( check ) {
                ret_val = false;
            }
        }
    } else {
        ret_val = false;
    }

    return ret_val;
}

std::string TE::CombineShares( const Ciphertext& ciphertext,
    const std::vector< std::pair< libff::alt_bn128_G2, size_t > >& decryptionShares ) {
    libff::alt_bn128_G2 U = std::get< 0 >( ciphertext );

    std::string V = std::get< 1 >( ciphertext );

    libff::alt_bn128_G1 W = std::get< 2 >( ciphertext );

    libff::alt_bn128_G1 H = this->HashToGroup( U, V );

    libff::alt_bn128_GT fst, snd;
    fst = libff::alt_bn128_ate_reduced_pairing( W, libff::alt_bn128_G2::one() );
    snd = libff::alt_bn128_ate_reduced_pairing( H, U );

    bool res = fst == snd;

    if ( !res ) {
        throw std::runtime_error( "error during share combining" );
    }

    std::vector< int > idx( this->t_ );
    for ( size_t i = 0; i < this->t_; ++i ) {
        idx[i] = decryptionShares[i].second;
    }

    std::vector< libff::alt_bn128_Fr > lagrange_coeffs = this->LagrangeCoeffs( idx );

    libff::alt_bn128_G2 sum = libff::alt_bn128_G2::zero();
    for ( size_t i = 0; i < this->t_; ++i ) {
        libff::alt_bn128_G2 temp = lagrange_coeffs[i] * decryptionShares[i].first;

        sum = sum + temp;
    }

    std::string hash = this->Hash( sum );

    std::valarray< uint8_t > lhs_to_hash( hash.size() );
    for ( size_t i = 0; i < hash.size(); ++i ) {
        lhs_to_hash[i] = static_cast< uint8_t >( hash[i] );
    }

    std::valarray< uint8_t > rhs_to_hash( V.size() );
    for ( size_t i = 0; i < V.size(); ++i ) {
        rhs_to_hash[i] = static_cast< uint8_t >( V[i] );
    }

    std::valarray< uint8_t > xor_res = lhs_to_hash ^ rhs_to_hash;

    std::string message = "";
    for ( size_t i = 0; i < xor_res.size(); ++i ) {
        message += static_cast< char >( xor_res[i] );
    }

    return message;
}

std::vector< libff::alt_bn128_Fr > TE::LagrangeCoeffs( const std::vector< int >& idx ) {
    if ( idx.size() < this->t_ ) {
        // throw IncorrectInput( "not enough participants in the threshold group" );
        throw std::runtime_error( "not enough participants in the threshold group" );
    }

    std::vector< libff::alt_bn128_Fr > res( this->t_ );

    libff::alt_bn128_Fr w = libff::alt_bn128_Fr::one();

    for ( size_t i = 0; i < this->t_; ++i ) {
        w *= libff::alt_bn128_Fr( idx[i] );
    }

    for ( size_t i = 0; i < this->t_; ++i ) {
        libff::alt_bn128_Fr v = libff::alt_bn128_Fr( idx[i] );

        for ( size_t j = 0; j < this->t_; ++j ) {
            if ( j != i ) {
                if ( libff::alt_bn128_Fr( idx[i] ) == libff::alt_bn128_Fr( idx[j] ) ) {
                    // throw IncorrectInput(
                    //     "during the interpolation, have same indexes in list of indexes" );
                    throw std::runtime_error(
                        "during the interpolation, have same indexes in list of indexes" );
                }

                v *= ( libff::alt_bn128_Fr( idx[j] ) -
                       libff::alt_bn128_Fr( idx[i] ) );  // calculating Lagrange coefficients
            }
        }

        res[i] = w * v.invert();
    }

    return res;
}

}  // namespace encryption
