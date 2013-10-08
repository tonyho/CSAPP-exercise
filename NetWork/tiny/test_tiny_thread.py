import string, threading, time
import httplib 
def thread_main(a):

   threadname = threading.currentThread().getName()
   for x in xrange(0, a):
      conn = httplib.HTTPConnection("127.0.0.1",8080)
      conn.request('get', '/log.txt')
      print conn.getresponse().read()[10]
      conn.close()
 
def main(num):
    global count, mutex
    threads = []
 
    count = 1
    mutex = threading.Lock()

    for x in xrange(0, num):
        threads.append(threading.Thread(target=thread_main, args=(10,)))

    for t in threads:
        t.start()
    for t in threads:
        t.join()  
 
 
if __name__ == '__main__':
    main(500)
