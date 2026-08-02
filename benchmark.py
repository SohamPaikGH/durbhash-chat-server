import signal
import socket
import sys
import threading
import time

HOST = "127.0.0.1"
PORT = 50000
MAX_CLIENTS = 50000

def connect(client_id: int):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # s.settimeout(5)
    try:
        # print(f"Attempting to connect as {client_id}...")
        s.connect((HOST, PORT))
        # print(f"{client_id} has connected!")
        while True:
            data = s.recv(1024)
            if data:
                break
        # print(f"{client_id} received \'{data}\'!")
        name_msg = f"{client_id}\n".encode()
        # print(f"{client_id} sent \'{name_msg}\'!")
        s.send(name_msg)
        data = s.recv(1024)
        while data:
            data = s.recv(1024)
            # print(client_id, ":", data)
    except Exception as e:
        print(e)
    finally:
        s.close()

def max_conn():
    threads = []
    counter = 0
    for i in range(MAX_CLIENTS):
        t = threading.Thread(target=connect, args=(i,))
        t.start()
        threads.append(t)
        counter += 1
        if counter % 500 == 0:
            print(f"{counter} connections")

def run_one_test() -> float:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    while True:
        data = s.recv(1024)
        if data:
            break
    s.send(b"test\n")
    start_time = time.perf_counter()
    s.send(b"Hello World!\n")
    while True:
        data = s.recv(1024)
        if data:
            break
    end_time = time.perf_counter()
    execution_time = end_time - start_time
    s.send(b"quit\n")
    return execution_time

def average_latency() -> float:
    sum = 0
    for _ in range(10):
        sum += run_one_test()
    return sum / 10.0

if len(sys.argv) < 2:
    print("ERROR: Too few arguments!")
else:
    if sys.argv[1] == "latency":
        print(f"Average Latency: {average_latency():.8f}s")
    elif sys.argv[1] == "connections":
        max_conn()
    elif sys.argv[1] == "one_test":
        print(run_one_test())
