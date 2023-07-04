#!/usr/bin/python3
# -*- coding: utf-8 -*-
 
import codecs
import sys
import hashlib
from tree_lib import Node
 
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

        tree = None
 
        for line in ima_entries_array:

            line = line.strip()
            if line == '':
                continue
 
            tokens = line.split()

            # initialization of the tree with the id of the host's ima namespace
            if tree == None and tokens[-1] != 0:
                tree = Node(tokens[-1]) 

            template_hash = codecs.decode(tokens[1], 'hex')

            if tokens[2] == 'ns-event':

                if str.isdigit(tokens[4]) and str.isdigit(tokens[5]):
                    parent = int(tokens[4])
                    child = int(tokens[5])
                else:
                    print("wrong ima log structure")
                    break

                if tokens[3] == 0:
                    # creation of a namespace, insertion in the tree
                    tree.add_child_between(tree.find_in_childs(parent), tree.find_in_childs(child))
                elif tokens[3] == 1:
                    # closure of the namespace
                    tree.close_ns(child)
                else:
                    print("wrong ima log structure")
                    break
 
            if template_hash == start_hash[hash_alg]:
                template_hash = ff_hash[hash_alg]
 
            if tokens[2] == "ima-id":
                # the value comes from a child ima_ns so have to be extended multiple times
                number_of_extensions = tree.node_heigh(tree.find_in_childs(tokens[5]))

                hash_mes = tokens[3].split(':')
                byte_value = codecs.decode(hash_mes[1], 'hex')
                value_sha1 = hash_mes[0] + ':' +"\x00"
                value_to_hash = bytearray(value_sha1.encode('utf-8')) + byte_value

                file_path = tokens[4] + '\x00'
                value_to_hash = value_to_hash + bytearray(file_path.encode('utf-8'))

                value_to_hash = value_to_hash + bytearray((tokens[-1]))
                value_hashed = Hash.compute_hash(hash_alg, value_to_hash)

                for x in range(0,number_of_extensions):
                    runninghash = Hash.compute_hash(hash_alg, runninghash + value_hashed)
            else:
                runninghash = Hash.compute_hash(hash_alg, runninghash + template_hash)
 
            found_pcr = (codecs.encode(runninghash, 'hex').decode('utf-8').lower() == pcr_val.lower())
 
            if found_pcr is True:
            	print("PCR 10 validated correctly!")
                break

if not found_pcr:
    print("PCR 10 does NOT match!")
