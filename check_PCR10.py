#!/usr/bin/python3
# -*- coding: utf-8 -*-
 
import codecs
import sys
import hashlib
 
START_HASH = (codecs.decode('0'*40, 'hex'))
FF_HASH = (codecs.decode('f'*40, 'hex'))
 
START_HASH_256 = (codecs.decode('0'*64, 'hex'))
FF_HASH_256 = (codecs.decode('f'*64, 'hex'))
 
START_HASH_384 = (codecs.decode('0'*96, 'hex'))
FF_HASH_384 = (codecs.decode('f'*96, 'hex'))
 
START_HASH_512 = (codecs.decode('0'*128, 'hex'))
FF_HASH_512 = (codecs.decode('f'*128, 'hex'))
 
class Hash:
    SHA1 = 'sha1'
    SHA256 = 'sha256'
    SHA384 = 'sha384'
    SHA512 = 'sha512'
    supported_algorithms = (SHA1, SHA256, SHA384, SHA512)
 
    @staticmethod
    def is_recognized(algorithm):
        return algorithm in Hash.supported_algorithms
 
    @staticmethod
    def compute_hash(algorithm, tohash):
        return {
                Hash.SHA1: lambda h: hashlib.sha1(h).digest(),
                Hash.SHA256: lambda h: hashlib.sha256(h).digest(),
                Hash.SHA384: lambda h: hashlib.sha384(h).digest(),
                Hash.SHA512: lambda h: hashlib.sha512(h).digest(),
            }[algorithm](tohash)
 
start_hash = {
            Hash.SHA1: START_HASH,
            Hash.SHA256: START_HASH_256,
            Hash.SHA384: START_HASH_384,
            Hash.SHA512: START_HASH_512
        }
ff_hash = {
        Hash.SHA1: FF_HASH,
        Hash.SHA256: FF_HASH_256,
        Hash.SHA384: FF_HASH_384,
        Hash.SHA512: FF_HASH_512
    }
 
if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Required PCR10 value as parameter")
        sys.exit(1)
 
    found_pcr = False
 
    pcr_val = sys.argv[1]
 
    hash_alg = Hash.SHA1
 
    runninghash = start_hash[hash_alg]
 
    with open('/sys/kernel/security/ima/ascii_runtime_measurements', 'r', encoding="utf-8") as ima_log_file:
        ima_data = ima_log_file.read()
        ima_entries_array = ima_data.split("\n")
 
        for line in ima_entries_array:
            line = line.strip()
            if line == '':
                continue
 
            tokens = line.split()
            if len(tokens) < 5:
                logger.error("invalid measurement list file line, number of fields < 5: -%s-" % (line))
                sys.exit(-1)
 
            template_hash = codecs.decode(tokens[1], 'hex')
 
 
            if template_hash == start_hash[hash_alg]:
                template_hash = ff_hash[hash_alg]
 
            if int(tokens[-1]) > 1 and tokens[2] == "ima-nsid-cnt":
                # the value comes from a child ima_ns so have to be extended multiple times
                number_of_extensions = int(tokens[-1])
                for x in range(1,number_of_extensions):
 
                    value_4 = tokens[-4].split(':')
                    byte_value = codecs.decode(value_4[1], 'hex')
                    value_sha1 = value_4[0] + ':' +"\x00"
                    value_to_hash = bytearray(value_sha1.encode('utf-8')) + byte_value
 
                    value_3 = tokens[-3] + '\x00'
                    value_to_hash = value_to_hash + bytearray(value_3.encode('utf-8'))
 
                    value_to_hash = value_to_hash + bytearray((tokens[-2] + str(x)).encode('utf-8'))
                    value_hashed = Hash.compute_hash(hash_alg, value_to_hash)
                    runninghash = Hash.compute_hash(hash_alg, runninghash + value_hashed)
 
            runninghash = Hash.compute_hash(hash_alg, runninghash + template_hash)
 
            found_pcr = (codecs.encode(runninghash, 'hex').decode('utf-8').lower() == pcr_val.lower())
 
            if found_pcr is True:
            	print("PCR 10 validated correctly!")
            	break;
 
if not found_pcr:
	print("PCR 10 does NOT match!")
 