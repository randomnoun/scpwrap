
# scpwrap

**scpwrap**  is a CLI program that converts scp output into javascript.

It's used to create progress bars in the unlikely case that you're piping the output of a bash script directly into a browser, which is pretty insecure and no-one should ever do, ever. 

## Why would anyone want to do that  ?

See http://www.randomnoun.com/wp/2013/10/31/progress-bars/

## Licensing
scpwrap is licensed under the BSD 2-clause license.

## Caveats
* It only understands scp output. For a more general solution, you probably want pv instead.   
   * Although the last time I looked, that only creates text
   * But that's probably good enough
   * For your 5-line bash script that involves copying a lot of data
   * That you want to put on a website

