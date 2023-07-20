import subprocess

def run_bash_command(command):
    try:
        result = subprocess.check_output(command, shell=True, stderr=subprocess.STDOUT, text=True)
        return result.strip()
    except subprocess.CalledProcessError as e:
        return f"Error: {e.returncode}\n{e.output.strip()}"

# Example command
bash_command = "sudo bash ./ima_ns_id_mapping.sh 681d11002c073b6a9227a48750d1171ec7fbbb25ce579eb5dec3e7b4813b03bb"

# Run the Bash command and get the output
output = run_bash_command(bash_command)

print("Output:", output)
