/****************************************************************************
 * chips/bk7258/bk7258_ca.c
 *
 * Root CAs for the HTTPS endpoints the agent talks to:
 *  - cn-beijing.maas.aliyuncs.com: issuer "GlobalSign RSA OV SSL CA 2018",
 *    which chains to "GlobalSign Root CA - R3" (plus R46 for lifetime).
 *  - ark.cn-beijing.volces.com: issuer "Xiaomi Intermediate CA", which
 *    chains to "Xiaomi Root CA".
 *
 * Embedded as a string because the AP has only tmpfs: there is no
 * persistent filesystem to read a bundle from, and the ai_agent's TLS
 * stack must verify the server (packages/ai_agent/src/infra/vela_tls.c
 * uses agent_ca_pem(), a weak hook this file provides).
 *
 * Regenerate from the system bundle /etc/ssl/certs/ca-certificates.crt;
 * do not edit by hand.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>

const char bk7258_ca_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G\n"
    "A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp\n"
    "Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4\n"
    "MTAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG\n"
    "A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI\n"
    "hvcNAQEBBQADggEPADCCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8\n"
    "RgJDx7KKnQRfJMsuS+FggkbhUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT\n"
    "gHeMCOFJ0mpiLx9e+pZo34knlTifBtc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm\n"
    "KPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yLy6c+9C7v/U9AOEGM+iCK65TpjoWc4zd\n"
    "QQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdbKfd/+RFO+uIEn8rUAVSNECMWEZ\n"
    "XriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F2xmmFghcCAwEAAaNCMEAw\n"
    "DgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFI/wS3+o\n"
    "LkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAUKr+yAzv95ZU\n"
    "RUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr25sbwMp\n"
    "jjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJCK\n"
    "6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX\n"
    "mcIfeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs\n"
    "Mx86OyXShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH\n"
    "WD9f\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\n"
    "MIIENzCCAx+gAwIBAgIUdPFE4H51TBcVkmI+i2+A4n5RdxkwDQYJKoZIhvcNAQEL\n"
    "BQAwdDELMAkGA1UEBhMCQ04xEDAOBgNVBAgTB0JlaWppbmcxEDAOBgNVBAcTB0Jl\n"
    "aWppbmcxFDASBgNVBAoTC1hpYW9taSBJbmMuMRIwEAYDVQQLEwlYaWFvbWkgQ0Ex\n"
    "FzAVBgNVBAMTDlhpYW9taSBSb290IENBMCAXDTIwMTAyODA2MTcwMFoYDzIwNTAx\n"
    "MDIxMDYxNzAwWjB0MQswCQYDVQQGEwJDTjEQMA4GA1UECBMHQmVpamluZzEQMA4G\n"
    "A1UEBxMHQmVpamluZzEUMBIGA1UEChMLWGlhb21pIEluYy4xEjAQBgNVBAsTCVhp\n"
    "YW9taSBDQTEXMBUGA1UEAxMOWGlhb21pIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEB\n"
    "AQUAA4IBDwAwggEKAoIBAQC8a9sI7MfELldYIkVGrtTyc8P9btEUYmkltPZRfpRg\n"
    "B6PwC4kYp0ccmWaGg0oSMdJ/+kKg0HdwxHoSHlGyO/9JU93ONlqEeZGP96I6B2us\n"
    "28nNdT29bjDixUEu5IVZ++++n3XYVQrDzeiLQxO+mmOss5/Of+Gn8G2rGUTN9RB3\n"
    "Sqk+Gf23kJ6ADzN3JqGaxQ1XZvZ7fItBxrKNw88KeVu55pofVvR+MrahK+yRKKwz\n"
    "ciJ0fM6p+sjWd4WRIHW0m/oQDNmZm1RhVyffFeTDLy91jxB3ljV+O7DDfu438bYy\n"
    "wv1PrOKsME7se/7Ib/N5KQcX2FrtzacebvXd2lrY4CJZAgMBAAGjgb4wgbswDgYD\n"
    "VR0PAQH/BAQDAgGmMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFA+s1g4nFpXV\n"
    "76x4RAOLjWDch/NSMEEGCCsGAQUFBwEBBDUwMzAxBggrBgEFBQcwAoYlaHR0cDov\n"
    "L2NhLm1pb2ZmaWNlLmNuL2NhZGF0YS9yb290LnBlbTA2BgNVHR8ELzAtMCugKaAn\n"
    "hiVodHRwOi8vY2EubWlvZmZpY2UuY24vY2FkYXRhL3Jvb3QuY3JsMA0GCSqGSIb3\n"
    "DQEBCwUAA4IBAQARyzYtcVyN10DYBI9tteH8qboGFD27zaOz2UYdLdzwaOEJ0cc9\n"
    "hxXksVE3ynyRyppGGq4p9caHS9YqCZyTurijeOlyWYZjcRe2N/zZzqCTjQn5iIPi\n"
    "70fcl+QURi3zy20FLA4yYAHkK9pxmVRVVkNK/yTx1POoc83Jxq0VCv81gkTNiJCN\n"
    "zk641VMFw2ACPCY+eIQzcAbVCgtDuzQ283f90PqZV5b3klLHNhhtYGwjY0pdiJAg\n"
    "uRYa9tWSYIzU13D5qtyntCdVLM/hTSduvxASewUNZlaSx/zFXMxqEo//AmER+IRX\n"
    "zKpxPLgZv7HPDs6S0wtkELRzmBMgUGF6FVGs\n"
    "-----END CERTIFICATE-----\n";

const unsigned int bk7258_ca_pem_len = sizeof(bk7258_ca_pem) - 1u;

/* Referenced from board_late_initialize() so the archive member is pulled
 * in: nothing else names these symbols, and the weak fallback in
 * vela_tls.c would otherwise satisfy the reference on its own.
 */

const char *agent_ca_pem(void)
{
  return bk7258_ca_pem;
}

void bk7258_ca_init(void)
{
  (void)bk7258_ca_pem_len;
}
