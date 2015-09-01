# Introduction #

If you want to build your own packages (Debian or RPM) from the source, read on.


## Get the Source ##

You can either get the source as a tarball off the front page, or download a development branch from the Subversion repository.

If you want to checkout an svn copy, you would want to issue a command similar to:

```
svn co http://fpac.googlecode.com/svn/branches/3.27 fpac-3.27
```

This will download the development source in to a subfolder called fpac-3.27.


## Prepare the Source ##

After downloading the source, run the autogen.sh script to build the necessary configuration tools.


## Compile the Source ##

If you want to just compile normally at this point:

```
./configure
make
make install
make installconf
```


## Build a Debian Package ##

If, instead, you want to build a Debian package from the development source, run the following command:

```
debuild -i -us -uc -b
```

This will build a Debian binary package in the next higher level folder.

Note, you're going to need the necessary Debian package building software/scripts on your system in order to do this.