#!/bin/bash
# remove existing updfpac
  /bin/rm ./updfpac.*
# Download last updfpac scripts
  wget http://f6bvp.org/updfpac.sh
  wget http://f6bvp.org/updfpac
#
  chmod 775 updfpac.sh
  chmod 775 updfpac
  exec ./updfpac
#
