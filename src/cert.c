/* cert.c - main function for the certificate handling
 *      Copyright (C) 2001 g10 Code GmbH
 *
 * This file is part of KSBA.
 *
 * KSBA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * KSBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "util.h"
#include "ber-decoder.h"
#include "convert.h"
#include "keyinfo.h"
#include "cert.h"


/**
 * ksba_cert_new:
 * 
 * Create a new and empty certificate object
 * 
 * Return value: A cert object or NULL in case of memory problems.
 **/
KsbaCert
ksba_cert_new (void)
{
  KsbaCert cert;

  cert = xtrycalloc (1, sizeof *cert);
  if (!cert)
    return NULL;


  return cert;
}

/**
 * ksba_cert_release:
 * @cert: A certificate object
 * 
 * Release a certificate object.
 **/
void
ksba_cert_release (KsbaCert cert)
{
  /* FIXME: release cert->root, ->asn_tree */
  xfree (cert);
}


/**
 * ksba_cert_read_der:
 * @cert: An unitialized certificate object
 * @reader: A KSBA Reader object
 * 
 * Read the next certificate from the reader and store it in the
 * certificate object for future access.  The certificate is parsed
 * and rejected if it has any syntactical or semantical error
 * (i.e. does not match the ASN.1 description).
 * 
 * Return value: 0 on success or an error value
 **/
KsbaError
ksba_cert_read_der (KsbaCert cert, KsbaReader reader)
{
  KsbaError err = 0;
  BerDecoder decoder = NULL;

  if (!cert || !reader)
    return KSBA_Invalid_Value;
  if (cert->initialized)
    return KSBA_Conflict; /* FIXME: should remove the old one */

  /* fixme: clear old cert->root */

  err = ksba_asn_create_tree ("tmttv2", &cert->asn_tree);
  if (err)
    goto leave;

  decoder = _ksba_ber_decoder_new ();
  if (!decoder)
    {
      err = KSBA_Out_Of_Core;
      goto leave;
    }

  err = _ksba_ber_decoder_set_reader (decoder, reader);
  if (err)
    goto leave;
  
  err = _ksba_ber_decoder_set_module (decoder, cert->asn_tree);
  if (err)
     goto leave;

  err = _ksba_ber_decoder_decode (decoder, "TMTTv2.Certificate",
                                  &cert->root, &cert->image, &cert->imagelen);
  if (!err)
      cert->initialized = 1;
  
 leave:
  _ksba_ber_decoder_release (decoder);

  return err;
}


KsbaError
ksba_cert_init_from_mem (KsbaCert cert, const void *buffer, size_t length)
{
  KsbaError err;
  KsbaReader reader;

  reader = ksba_reader_new ();
  if (!reader)
    return KSBA_Out_Of_Core;
  err = ksba_reader_set_mem (reader, buffer, length);
  if (err)
    {
      ksba_reader_release (reader);
      return err;
    }
  err = ksba_cert_read_der (cert, reader);
  ksba_reader_release (reader);
  return err;
}



const unsigned char *
ksba_cert_get_image (KsbaCert cert, size_t *r_length )
{
  AsnNode n;

  if (!cert)
    return NULL;
  if (!cert->initialized)
    return NULL;

  n = _ksba_asn_find_node (cert->root, "Certificate");
  if (!n) 
    return NULL;

  if (n->off == -1)
    {
      fputs ("ksba_cert_get_image problem at node:\n", stderr);
      _ksba_asn_node_dump_all (n, stderr);
      return NULL;
    }

  if (r_length)
    *r_length = cert->imagelen;
  return cert->image;
}


KsbaError
ksba_cert_hash (KsbaCert cert, int what,
                void (*hasher)(void *, const void *, size_t length), 
                void *hasher_arg)
{
  AsnNode n;

  if (!cert /*|| !hasher*/)
    return KSBA_Invalid_Value;
  if (!cert->initialized)
    return KSBA_No_Data;

  n = _ksba_asn_find_node (cert->root,
                           what == 1? "Certificate.tbsCertificate"
                                    : "Certificate");
  if (!n)
    return KSBA_No_Value; /* oops - should be there */
  if (n->off == -1)
    {
      fputs ("ksba_cert_hash problem at node:\n", stderr);
      _ksba_asn_node_dump_all (n, stderr);
      return KSBA_No_Value;
    }

  hasher (hasher_arg, cert->image + n->off,  n->nhdr + n->len);


  return 0;
}


/**
 * ksba_cert_get_digest_algo:
 * @cert: Initialized certificate object
 * 
 * Figure out the the digest algorithm used for the signature and
 * return it as a number suitable to be used to identify a digest
 * algorithm in Libgcrypt.
 *
 * This function is intended as a helper for the ksba_cert_hash().
 * 
 * Return value: 0 for error or unknown algoritm, otherwise a
 * GCRY_MD_xxx constant.
 **/
int
ksba_cert_get_digest_algo (KsbaCert cert)
{
  AsnNode n;
  int algo;

  if (!cert)
    {
       cert->last_error = KSBA_Invalid_Value;
       return 0;
    }
  if (!cert->initialized)
    {
       cert->last_error = KSBA_No_Data;
       return 0;
    }

  n = _ksba_asn_find_node (cert->root,
                           "Certificate.signatureAlgorithm.algorithm");
  algo = _ksba_node_with_oid_to_digest_algo (cert->image, n);
  if (!algo)
    cert->last_error = KSBA_Unknown_Algorithm;
  else if (algo == -1)
    {
      cert->last_error = KSBA_No_Value;
      algo = 0;
    }

  return algo;
}




/**
 * ksba_cert_get_serial:
 * @cert: certificate object 
 * 
 * This function returnes the serial number of the certificate.  The
 * serial number is an integer returned in a buffer formatted in a
 * format like the one used by SSH: The first 4 bytes are to be
 * considered the length of the following integer bytes in network
 * byte order, the integer itself is in 2's complement.  This format
 * can be passed to gcry_mpi_scan() when a length of 0 is given.  The
 * caller must free the buffer.
 * 
 * Return value: An allocated buffer or NULL for no value.
 **/
unsigned char *
ksba_cert_get_serial (KsbaCert cert)
{
  AsnNode n;
  char *p;

  if (!cert || !cert->initialized)
    return NULL;
  
  n = _ksba_asn_find_node (cert->root,
                           "Certificate.tbsCertificate.serialNumber");
  if (!n)
    return NULL; /* oops - should be there */

  if (n->off == -1)
    {
      fputs ("get_serial problem at node:\n", stderr);
      _ksba_asn_node_dump_all (n, stderr);
      return NULL;
    }
  
  p = xtrymalloc ( n->len + 4 );
  return_null_if_fail (p);

  p[0] = n->len >> 24;
  p[1] = n->len >> 16;
  p[2] = n->len >> 8;
  p[3] = n->len;
  memcpy (p+4, cert->image + n->off + n->nhdr, n->len);
  return p;
}

/**
 * ksba_cert_get_issuer:
 * @cert: certificate object
 * 
 * Returns the Distinguished Name (DN) of the certificate issuer which
 * in most cases is a CA.  The format of the returned string is in
 * accordance with RFC-2253.  NULL is returned if the DN is not
 * available which is an error and should have been catched by the
 * certificate reading function.
 * 
 * The caller must free the returned string using ksba_free() or the
 * function he has registered as a replacement.
 * 
 * Return value: An allocated string or NULL for error.
 **/
char *
ksba_cert_get_issuer (KsbaCert cert)
{
  KsbaError err;
  AsnNode n;
  char *p;

  if (!cert || !cert->initialized)
    return NULL;
  
  n = _ksba_asn_find_node (cert->root,
                           "Certificate.tbsCertificate.issuer");
  if (!n || !n->down)
    return NULL; /* oops - should be there */
  n = n->down; /* dereference the choice node */

  if (n->off == -1)
    {
      fputs ("get_issuer problem at node:\n", stderr);
      _ksba_asn_node_dump_all (n, stderr);
      return NULL;
    }
  err = _ksba_dn_to_str (cert->image, n, &p);
  if (err)
    {
      cert->last_error = err;
      return NULL;
    }
  return p;
}



/**
 * ksba_cert_get_valididy:
 * @cert: cetificate object
 * @what: 0 for notBefore, 1 for notAfter
 * 
 * Return the validity object from the certificate.  If no value is
 * available 0 is returned becuase we can safely assume that this is
 * not a valid date.
 * 
 * Return value: seconds since epoch, 0 for no value or (time)-1 for error.
 **/
time_t
ksba_cert_get_validity (KsbaCert cert, int what)
{
  AsnNode n, n2;
  time_t t;

  if (!cert || what < 0 || what > 1)
    return (time_t)(-1);
  if (!cert->initialized)
    return (time_t)(-1);
  
  n = _ksba_asn_find_node (cert->root,
        what == 0? "Certificate.tbsCertificate.validity.notBefore"
                 : "Certificate.tbsCertificate.validity.notAfter");
  if (!n)
    return 0; /* no value available */

  /* FIXME: We should remove the choice node and don't use this ugly hack */
  for (n2=n->down; n2; n2 = n2->right)
    {
      if ((n2->type == TYPE_UTC_TIME || n2->type == TYPE_GENERALIZED_TIME)
          && n2->off != -1)
        break;
    }
  n = n2;
  if (!n)
    return 0; /* no value available */

  return_val_if_fail (n->off != -1, (time_t)(-1));

  t = _ksba_asntime_to_epoch (cert->image + n->off + n->nhdr, n->len);
  if (!t) /* we consider this an error */
    t = (time_t)(-1);
  return t;
}


/* See ..get_issuer */
char *
ksba_cert_get_subject (KsbaCert cert)
{
  KsbaError err;
  AsnNode n;
  char *p;

  if (!cert || !cert->initialized)
    return NULL;
  
  n = _ksba_asn_find_node (cert->root,
                           "Certificate.tbsCertificate.subject");
  if (!n || !n->down)
    return NULL; /* oops - should be there */
  n = n->down; /* dereference the choice node */

  if (n->off == -1)
    {
      fputs ("get_issuer problem at node:\n", stderr);
      _ksba_asn_node_dump_all (n, stderr);
      return NULL;
    }
  err = _ksba_dn_to_str (cert->image, n, &p);
  if (err)
    {
      cert->last_error = err;
      return NULL;
    }
  return p;
}


char *
ksba_cert_get_public_key (KsbaCert cert)
{
  AsnNode n;
  KsbaError err;
  char *string;

  if (!cert)
    return NULL;
  if (!cert->initialized)
    return NULL;

  n = _ksba_asn_find_node (cert->root,
                           "Certificate"
                           ".tbsCertificate.subjectPublicKeyInfo");
  if (!n)
    {
      cert->last_error = KSBA_No_Value;
      return NULL;
    }

  err = _ksba_keyinfo_to_sexp (cert->image + n->off, n->nhdr + n->len,
                               &string);
  if (err)
    {
      cert->last_error = err;
      return NULL;
    }

  return string;
}

char *
ksba_cert_get_sig_val (KsbaCert cert)
{
  AsnNode n, n2;
  KsbaError err;
  char *string;

  if (!cert)
    return NULL;
  if (!cert->initialized)
    return NULL;

  n = _ksba_asn_find_node (cert->root,
                           "Certificate.signatureAlgorithm");
  if (!n)
    {
      cert->last_error = KSBA_No_Value;
      return NULL;
    }
  if (n->off == -1)
    {
      fputs ("ksba_cert_get_sig_val problem at node:\n", stderr);
      _ksba_asn_node_dump_all (n, stderr);
      cert->last_error = KSBA_No_Value;
      return NULL;
    }

  n2 = n->right;
  err = _ksba_sigval_to_sexp (cert->image + n->off,
                              n->nhdr + n->len
                              + ((!n2||n2->off == -1)? 0:(n2->nhdr+n2->len)),
                              &string);
  if (err)
    {
      cert->last_error = err;
      return NULL;
    }

  return string;
}





