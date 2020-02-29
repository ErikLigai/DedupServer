# Design Issues and Questions
* ### What is the internal data structure you use to map file names to file contents?
    * I used two data structures in the form of linked-lists to store the file names to our file contents. Originally, I wanted to implement a hash table, but the complexities of generating a viable hash and the overhead made it seem unreasonable. Therefore, I elected to use linked lists to store both the hashnames and filenames. Effectively, it becomes a 2D linked-list. The issues I ran into were that traversal of data is slow, since I would have to look up hashenames first and then their respective filenames.
<br><br />
* ### How do you ensure that the server is responding efficiently and quickly to client requests?
    * I create threads to service client processes, and handle efficient requests through synchronization logic such as mutex locks and condition variables. The condition variables that I had in place check for whether a client is trying to access a file in the server that is already in use. If it is, I block the thread and let another process continue with access to the mutex lock.
<br><br />
* ### How do you ensure changes to your internal data structures are consistent when multiple threads try to perform such changes?
    * As said before, I use a mutex lock and condition variables when any file is being accessed by a client, which ensures that other clients can't make changes to those files.
<br><br />
* ### How are the contents of a file which is being uploaded stored before you can determine whether it matches the hash of already stored file(s)?
    * I store the files as the filenames that the client tried to upload with, since I know already that a file stored with the same name in the server would cause an error to be thrown.
<br><br />
* ### How do you deal with clients that fail or appear to be buggy without harming the consistency of your server?
    * I don't deal with clients that fail. I could have solved this by either creating an idle timer that checks when a client doesn't do anything, or by having a counter for each thread that checks how many times a user has caused an error on the server side.
<br><br />
* ### How do you handle the graceful termination of the server when it receives SIGTERM?
    * I use a signal handler to terminate the server, and have a function that saves the current state of the .dedup file in the form of writing back the XML to the file. Once this is done, I close the sockets by calling exit on the server. I don't notify client processes about such disconnection.
<br><br />
* ### What sanity checks do you perform when the server starts on a given directory?
    * I check if a .dedup file exists, otherwise I don't have sanity checks because the requirements state that I can assume that if the directory was empty, then I don't account for such files and I assume the directory to be empty.
<br><br />
* ### How do you deal with zero sized files?
    * I deal with zero-sized files by checking if the file contents is null, which if so, lets us open the file and generate the default md5 hash of d41d8cd98f00b204e9800998ecf8427e.
<br><br />