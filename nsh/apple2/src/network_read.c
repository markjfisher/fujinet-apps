/**
 * Read from channel 
 *
 * @param buf Buffer
 * @param len length
 * @return adamnet status code
 */

#include <string.h>
#include "network.h"
#include "sp.h"

unsigned short network_read(char *buf, unsigned short len)
{  
  unsigned short offset=0;

  memset(buf,0,len);
  
  while (len>0)
    {
      unsigned short i=len;

      if (len>sizeof(sp_payload))
	i=sp_payload;

      sp_read(&buf[offset],i);
      
      len -= i;
      offset += i;
    }
  
  return offset;
}
