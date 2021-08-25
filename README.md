# UCR-eBPF

## 1. Start up a node on Cloudlab
- When starting a new experiment on Cloudlab, select the small-lan profile
- In the profile parameterization page, 
  - Set Number of Nodes as 1 
  - Set OS image as Ubuntu 20.04 
  - Set physical node type as xl170 
  - Please check Temp Filesystem Max Space 
  - Keep Temporary Filesystem Mount Point as default (/mydata)

## 2. Extend the disk

Assume the temporary filesystem mount point is default (/mydata), please run
```
sudo chown -R $(id -u):$(id -g) /mydata
cd /mydata
git clone https://github.com/ShixiongQi/UCR-eBPF.git
cd /mydata/UCR-eBPF/
git checkout test
```

## 3. Update the system(both kernel and distribution)
Update the system using the script in the folder `/mydata/UCR-eBPF/`:
```
cd /mydata/UCR-eBPF/
./update_system.sh
```
- If there is an option, you can choose to use the maintainer's version.
- After the update, lease reboot the machine using ```sudo reboot```.
- After reboot, run the following command:
```
sudo apt-get autoremove
sudo apt-get clean
```
- Check the distribution and kernel version
```
lsb_release -a
uname -r
```

## 4. Install bpf library
After reboot, run the following command to install bpf library.
```
cd /mydata/UCR-eBPF/
./env_setup.sh
```

## 5. Setup veth pair
Please run the folowing command to setup veth pair.
```
cd /mydata/UCR-eBPF/test/knative/
./setup_veth.sh
```
- You can use ```ip a``` to check the result. There should be interfaces named "test" and "test1".

## 6. Compile the binaries [Optional]
Please run the following command to build all the binaries for the test.
```
cd /mydata/UCR-eBPF/test/xsk_bounce/
./build.sh
```

## 7. Run the test
- Prepare the test
```
cd /mydata/UCR-eBPF/test/xsk_bounce/
cp bounce_kern.o bounce rx_kern.o manager client libbpfclient.so ../knative/
sudo apt-get install golang-go
```
- Running the test. Please open 5 terminals and run the following commands.
  - Terminal 1
    ```
    cd /mydata/UCR-eBPF/test/knative/
    sudo ./bounce test1
    ```
  - Terminal 2
    ```
    cd /mydata/UCR-eBPF/test/knative/
    sudo ./manager test
    ```
  - Terminal 3
    ```
    cd /mydata/UCR-eBPF/test/knative/
    sudo ./client test recv 1 --last
    ```
  - Terminal 4
    ```
    cd /mydata/UCR-eBPF/test/knative/
    sudo go run ./helloworld.go
    ```
  - Terminal 5
    ```
    cd /mydata/UCR-eBPF/test/knative/
    curl http://localhost:8080
    ```
    You can run ```watch -n 1 curl http://localhost:8080``` in terminal 5 for continually testing.
