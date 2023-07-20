#!/bin/bash

# Return the ima_ns_id for the container
# 1 - wrong arguments
# 2 - Container not contained in the host
# 3 - Container not running or not abilitated the ima_ns

extract_number_from_brackets() {
    local input_string="${1#"["}"
    input_string="${input_string%"]"}"
    
    echo "$input_string"
}

if [ $# -ne 1 ]; then
    echo "Error: Exactly one argument is required."
    exit 1
fi
cont_id="$1"
firstchars="${cont_id:0:12}"

dockerps=$(sudo -u lo docker ps -a | grep -i "${firstchars}")

if [ "$dockerps" == "" ]; then
    echo "The container is not contained in the host"
    exit 2
fi

dockerval=$(sudo -u lo docker container inspect $1 | grep -m 1 '"Pid": ')

docker_pid=$(echo "$dockerval" | awk -F':' '{print $2}')
pid=$(echo "$docker_pid" | tr -d ' ,')

if [ "$pid" == 0 ]; then
    echo "No pid found, container may be not running or not abilitate to create user namespaces."
    exit 3
fi

output=$(ls -la /proc/$pid/ns | grep -i "ima_ns") 

tokens=( $output )
userns="${tokens[10]}"

OLD_IFS=$IFS
IFS=":" read -ra value <<< "$userns"
IFS=$OLD_IFS

nsbracket=${value[1]}

number=$(extract_number_from_brackets "$nsbracket")

echo "${number}"

