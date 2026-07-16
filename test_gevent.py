import gevent.monkey
gevent.monkey.patch_all()
import time
import threading

def worker():
    print("Worker running")
    time.sleep(1)
    print("Worker done")

threading.Thread(target=worker).start()
print("Main done")
