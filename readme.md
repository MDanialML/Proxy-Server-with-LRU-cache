# Proxy Server with LRU Cache 🧠🌐

A multithreaded proxy server built in C that handles multiple client requests simultaneously and uses an LRU (Least Recently Used) caching strategy to store frequently accessed content. This reduces redundant data fetching, improves response time, and optimizes bandwidth usage.

---

## 🔧 Features

- ✅ Handles multiple client connections using POSIX threads (`pthread`)
- ✅ Semaphore-based concurrency control to limit active threads
- ✅ Implements an LRU cache to store and reuse previously accessed content
- ✅ Efficient memory and socket management
- ✅ Extracts and logs client IP and port information
- ✅ Dynamic buffer allocation for scalable data handling

---

## 🧩 Technologies Used

- **Language:** C
- **Concurrency:** POSIX Threads, Semaphores
- **Networking:** Sockets (`accept`, `connect`, `recv`, `send`)
- **Memory:** Dynamic allocation with `malloc` / `calloc`
- **Cache:** Custom LRU Cache Implementation

---

## 📁 Project Structure

proxy-server-lru/
├── main.c # Entry point: sets up socket and handles clients
├── thread_handler.c # Thread function to process each client
├── lru_cache.c # LRU cache logic and data structures
├── utils.c # Utility functions (logging, IP parsing, etc.)
├── Makefile # To compile the project
└── README.md # Project documentation

make
./proxy_server
Client is connected with port number 54321 and IP address is 192.168.0.10
Semaphore value is: 4
...
🧠 Future Enhancements
 Support for HTTPS

 Logging to files

 Configurable cache size

 Admin dashboard for monitoring connections and cache status

🙋‍♂️ Author
M Danial
📫 m.danial465@gmail.com
🔗 LinkedIn: https://www.linkedin.com/in/the-muhammad-danial/



