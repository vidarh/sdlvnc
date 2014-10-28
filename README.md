
# SDL VNC client #

This is a fork of [SDL_vnc](http://sourceforge.net/projects/sdlvnc/). 

It is **NOT** in what I'd consider a usable state yet. It's currently
in a lot of flux as I'm massaging the code into something that works
better for my purpose which is not necessarily compatible with that
of the original author.

For now chances are you'll be best of avoiding this repository, as
I'm not going to particularly worry about whether or not I break
stuff until I've cleaned up some more bits.

My intent is to:

 * Refactor the code.
 * Fix issues on 64 bit platforms (for now I pass -m32 to gcc)
 * Speed up the sockets handling (currently does lots of small
   read/writes)
 * Improve temporary buffer handling.
 * (possibly decouple from SDL)
 * Use it as a basis for some experimental VNC improvements for
   some specific use-cases I have in mind. More on that if/when I
   get there.

## Why (not) libvncclient? ##

If you want a usable VNC client right now, libvncclient is a much better
bet than this project. Any VNC client is a better bet than this project.

My reasons for picking SDL_vnc for my use is that it is really compact,
and also LGPL, while libvncclient is GPL. It also works, or at least
worked with very minimal adjustments when I started mucking around with it.
So it's a tiny, reasonably decent, starting point.




