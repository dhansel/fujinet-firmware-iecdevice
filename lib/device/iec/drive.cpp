#ifdef BUILD_IEC

#include "drive.h"

#include <cstring>
#include <sstream>
#include <unordered_map>

#include "../../include/debug.h"
#include "../../include/cbm_defines.h"

#include "make_unique.h"

#include "fuji.h"
#include "fnFsSD.h"
#include "led.h"
#include "utils.h"

#include "meat_media.h"


// Buffering data when reading/writing streams because during regular (non-fastloader)
// tranmissions, the read/write functions are called for each single byte at a time and
// reading/writing MStream one byte at a time can be time consuming.
// To be safe, BUFFER_SIZE should always be >=256
#define BUFFER_SIZE 512


#define ST_OK                  0
#define ST_SCRATCHED           1
#define ST_WRITE_ERROR        25
#define ST_WRITE_PROTECT      26
#define ST_SYNTAX_ERROR_31    31
#define ST_SYNTAX_ERROR_33    33
#define ST_FILE_NOT_OPEN      61
#define ST_FILE_NOT_FOUND     62
#define ST_FILE_EXISTS        63
#define ST_FILE_TYPE_MISMATCH 64
#define ST_NO_CHANNEL         70
#define ST_SPLASH             73
#define ST_DRIVE_NOT_READY    74


static bool isMatch(std::string name, std::string pattern)
{
  signed char found = -1;
  
  for(uint8_t i=0; found<0; i++)
    {
      if( pattern[i]=='*' )
        found = 1;
      else if( pattern[i]==0 || name[i]==0 )
        found = (pattern[i]==name[i]) ? 1 : 0;
      else if( pattern[i]!='?' && pattern[i]!=name[i] && !(name[i]=='~' && (pattern[i] & 0xFF)==0xFF) )
        found = 0;
    }
  
  return found==1;
}


// -------------------------------------------------------------------------------------------------


iecChannelHandler::iecChannelHandler(iecDrive *drive)
{ 
  m_drive = drive;
  m_data = new uint8_t[BUFFER_SIZE]; 
  m_len = 0; 
  m_ptr = 0; 
}


iecChannelHandler::~iecChannelHandler()
{ 
  delete [] m_data; 
}


uint8_t iecChannelHandler::read(uint8_t *data, uint8_t n)
{
  // if buffer is empty then re-fill it
  if( m_ptr >= m_len )
    {
      m_ptr = 0;
      m_len = 0;

      uint8_t st = readBufferData();
      if( st!=ST_OK )
        {
          m_drive->setStatusCode(st);
          return 0;
        }
    }

  // read data from buffer
  if( m_ptr < m_len )
    {
      if( n==1 )
        {
          // common case during regular (non-fastloader) load
          data[0] = m_data[m_ptr++];
          return 1;
        }
      else
        {
          // copy as much data as possible
          n = std::min((size_t) n, (size_t) (m_len - m_ptr));
          memcpy(data, m_data + m_ptr, n);
          m_ptr += n;
          return n;
        }
    }
  else
    return 0;
}


uint8_t iecChannelHandler::write(uint8_t *data, uint8_t n)
{
  // if buffer is full then empty it
  if( m_len+n > BUFFER_SIZE )
    {
      uint8_t st = writeBufferData();
      if( st!=ST_OK )
        {
          m_drive->setStatusCode(st);
          return 0;
        }

      m_ptr = 0;
      m_len = 0;
    }

  // write data to buffer
  if( n==1 )
    {
      // common case during regular (non-fastloader) save
      m_data[m_len++] = data[0];
    }
  else
    {
      // copy data
      memcpy(m_data + m_len, data, n);
      m_len += n;
    }

  return n;
}


// -------------------------------------------------------------------------------------------------


iecChannelHandlerFile::iecChannelHandlerFile(iecDrive *drive, MStream *stream, int fixLoadAddress) : iecChannelHandler(drive)
{
  m_stream = stream;
  m_fixLoadAddress = fixLoadAddress;
  m_timeStart = esp_timer_get_time();
  m_byteCount = 0;
  m_transportTimeUS = 0;
}


iecChannelHandlerFile::~iecChannelHandlerFile()
{
  double seconds = (esp_timer_get_time()-m_timeStart) / 1000000.0;

  if( m_stream->mode == std::ios_base::out && m_len>0 )
    writeBufferData();

  m_stream->close();
  Debug_printv("Stream closed.");

  double cps = m_byteCount / seconds;
  Debug_printv("%s %d bytes in %0.2f seconds @ %0.2fcps", m_stream->mode == std::ios_base::in ? "Sent" : "Received", m_byteCount, seconds, cps);

  double tseconds = m_transportTimeUS / 1000000.0;
  cps = m_byteCount / (seconds-tseconds);
  Debug_printv("Transport (network/sd) took %0.3f seconds, pure IEC transfers @ %0.2fcps", tseconds, cps);

  delete m_stream;
}


uint8_t iecChannelHandlerFile::writeBufferData()
{
  /*
  // if m_stream is within a disk image then m_stream->mode does not get initialized properly!
  if( m_stream->mode != std::ios_base::out )
    return ST_FILE_TYPE_MISMATCH;
  else
  */
    {
      Debug_printv("bufferSize[%d]", m_len);
      uint64_t t = esp_timer_get_time();
      size_t n = m_stream->write(m_data, m_len);
      m_transportTimeUS += (esp_timer_get_time()-t);
      m_byteCount += n;
      if( n<m_len )
        {
          Debug_printv("Error: write failed: n[%d] < m_len[%d]", n, m_len);
          return ST_WRITE_ERROR;
        }
    }
  
  return ST_OK;
}


uint8_t iecChannelHandlerFile::readBufferData()
{
  /*
  // if m_stream is within a disk image then m_stream->mode does not get initialized properly!
  if( m_stream->mode != std::ios_base::in )
    return ST_FILE_TYPE_MISMATCH;
  else
  */
    {
      Debug_printv("size[%d] avail[%d] pos[%d]", m_stream->size(), m_stream->available(), m_stream->position());

      if( m_fixLoadAddress>=0 && m_stream->position()==0 )
        {
          uint64_t t = esp_timer_get_time();
          m_len = m_stream->read(m_data, BUFFER_SIZE);
          m_transportTimeUS += (esp_timer_get_time()-t);
          if( m_len>=2 )
            {
              m_data[0] = (m_fixLoadAddress & 0x00FF);
              m_data[1] = (m_fixLoadAddress & 0xFF00) >> 8;
            }
          m_fixLoadAddress = -1;
        }
      else
        m_len = 0;

      // try to fill buffer
      while( m_len<BUFFER_SIZE && !m_stream->eos() )
        {
          uint64_t t = esp_timer_get_time();
          m_len += m_stream->read(m_data+m_len, BUFFER_SIZE-m_len);
          m_transportTimeUS += (esp_timer_get_time()-t);
        }

      m_byteCount += m_len;
    }

  return ST_OK;
}


// -------------------------------------------------------------------------------------------------


iecChannelHandlerDir::iecChannelHandlerDir(iecDrive *drive, MFile *dir) : iecChannelHandler(drive)
{
  m_dir = dir;
  m_headerLine = 1;
  
  std::string url = m_dir->host;
  url = mstr::toPETSCII2(url);
  std::string path = m_dir->path;
  path = mstr::toPETSCII2(path);
  std::string archive = m_dir->media_archive;
  archive = mstr::toPETSCII2(archive);
  std::string image = m_dir->media_image;
  image = mstr::toPETSCII2(image);
  
  m_headers.clear();
  if( url.size()>0 )     addExtraInfo("URL", url);
  if( path.size()>1 )    addExtraInfo("PATH", path);
  if( archive.size()>1 ) addExtraInfo("ARCHIVE", archive);
  if( image.size()>0 )   addExtraInfo("IMAGE", image);
  if( m_headers.size()>0 ) m_headers.push_back("NFO ----------------");
  
  // If SD Card is available and we are at the root path show it as a directory at the top
  if( fnSDFAT.running() && m_dir->url.size() < 2 )
    m_headers.push_back("DIR SD");
}


iecChannelHandlerDir::~iecChannelHandlerDir()
{
  delete m_dir;
}


void iecChannelHandlerDir::addExtraInfo(std::string title, std::string text)
{
  m_headers.push_back("NFO ["+title+"]");
  while( text.size()>0 )
    {
      std::string s = text.substr(0, 16);
      m_headers.push_back("NFO "+s);
      text.erase(0, s.size());
    }
}


uint8_t iecChannelHandlerDir::writeBufferData()
{
  return ST_FILE_TYPE_MISMATCH;
}


uint8_t iecChannelHandlerDir::readBufferData()
{
  if( m_headerLine==1 )
    {
      // main header line
      m_data[0] = 0x01;
      m_data[1] = 0x08;
      m_data[2] = 1;
      m_data[3] = 1;
      m_data[4] = 0;
      m_data[5] = 0;
      m_data[6] = 18;
      m_data[7] = '"';
      std::string name = m_dir->media_header.size() ? mstr::toPETSCII2(m_dir->media_header) : PRODUCT_ID;
      size_t n = std::min(16, (int) name.size());
      memcpy(m_data+8, name.data(), n);
      while( n<16 ) { m_data[8+n] = ' '; n++; }
      sprintf((char *) m_data+24, "\" %02i 2A", m_drive->id());
      m_len = 32;
      m_headerLine++;
    }
  else if( m_headerLine-2 < m_headers.size() )
    {
      // Send Extra INFO
      m_data[0] = 1;
      m_data[1] = 1;
      m_data[2] = 0;
      m_data[3] = 0;

      std::string name = m_headers[m_headerLine-2];
      std::string ext  = name.substr(0, 3);

      name = "   \"" + name.substr(4, 16) + "\"";
      if( name.size()<21 ) name += std::string(21-name.size(), ' ');
      name += " " + ext + "  ";
      strcpy((char *) m_data+4, name.c_str());
      m_data[31] = 0;
      m_len = 32;
      m_headerLine++;
    }
  else if( m_headerLine < 0xFF )
    {
      // file entries
      std::unique_ptr<MFile> entry; 

      // skip over files starting with "."
      do 
        { 
          entry = std::unique_ptr<MFile>( m_dir->getNextFileInDir() ); 
          if( entry!=nullptr ) Debug_printv("%s", entry->name.c_str());
        }
      while( entry!=nullptr && entry->name[0] == '.' );

      if( entry != nullptr )
        {
          // directory entry
          uint16_t size = entry->blocks();
          m_data[m_len++] = 1;
          m_data[m_len++] = 1;
          m_data[m_len++] = size&255;
          m_data[m_len++] = size/256;
          if( size<10 )    m_data[m_len++] = ' ';
          if( size<100 )   m_data[m_len++] = ' ';
          if( size<1000 )  m_data[m_len++] = ' ';

          std::string ext = entry->extension;
          mstr::ltrim(ext);
          if( entry->isDirectory() )
            ext = "dir";
          else if( ext.length()>0 )
            {
              if( ext.size()>3 )
                ext = ext.substr(0, 3);
              else
                ext += std::string(3-ext.size(), ' ');
            }
          else
            ext = "prg";

          std::string name = entry->name;
          if ( !entry->isPETSCII )
            {
              name = mstr::toPETSCII2( name );
              ext  = mstr::toPETSCII2( ext );
            }
          mstr::replaceAll(name, "\\", "/");
          
          m_data[m_len++] = '"';
          size_t n = std::min(16, (int) name.size());
          memcpy(m_data+m_len, name.data(), n);
          m_len += n;
          m_data[m_len++] = '"';
          n = 17-n;
          while(n-->0) m_data[m_len++] = ' ';
          memcpy(m_data+m_len, ext.data(), 3);
          m_len+=3;
          while( m_len<31 ) m_data[m_len++] = ' ';
          m_data[31] = 0;
          m_len = 32;
        }
      else
        {
          // no more entries => footer
          uint32_t free = m_dir->media_image.size() ? m_dir->media_blocks_free : 0;
          m_data[0] = 1;
          m_data[1] = 1;
          m_data[2] = free&255;
          m_data[3] = free/256;
          if( m_dir->media_image.size() )
            strcpy((char *) m_data+4, "BLOCKS FREE.");
          else
            sprintf((char *) m_data+4, CBM_DELETE CBM_DELETE "%sBYTES FREE.", mstr::formatBytes(m_dir->getAvailableSpace()).c_str());
          int n = 4+strlen((char *) m_data+4);
          while( n<29 ) m_data[n++]=' ';
          m_data[29] = 0;
          m_data[30] = 0;
          m_data[31] = 0;
          m_len = 32;
          m_headerLine = 0xFF;
        }
    }

  return ST_OK;
}


// -------------------------------------------------------------------------------------------------


iecDrive::iecDrive(uint8_t devnum) : IECFileDevice(devnum)
{
  m_host = nullptr;
  m_cwd.reset( MFSOwner::File("/") );
  m_statusCode = ST_SPLASH;
  m_statusTrk  = 0;
  m_numOpenChannels = 0;
  for(int i=0; i<16; i++) 
    m_channels[i] = nullptr;
}


iecDrive::~iecDrive()
{
  for(int i=0; i<16; i++)
    if( m_channels[i]!=nullptr )
      close(i);
}


void iecDrive::open(uint8_t channel, const char *cname)
{
  Debug_printv("iecDrive::open(#%d, %d, \"%s\")", m_devnr, channel, cname);
  
  // determine file name
  std::string name(cname);
  std::vector<std::string> pt = util_tokenize(name, ',');
  name = pt[0];
  if( mstr::startsWith(name, "0:") )
    name = mstr::drop(name, 2);

  // determine file mode (read/write)
  bool overwrite = false;
  std::ios_base::openmode mode = std::ios_base::in;
  if( channel==1 ) mode = std::ios_base::out;
  if( pt.size() >= 3 )
    {
      if( pt[2]=="R" )
        mode = std::ios_base::in;
      else if( pt[2]=="W" )
        mode = std::ios_base::out;
    }

  if( mstr::startsWith(name, "@:") )
    {
      overwrite = true;
      name = mstr::drop(name, 2);
    }      

  Debug_printv("opening channel[%d] m_cwd[%s] name[%s] mode[%s]", channel, m_cwd->url.c_str(), name.c_str(), 
               mode==std::ios_base::out ? (overwrite ? "replace" : "write") : "read");

  if( name.length()==0 )
    {
      Debug_printv("Error: empty file name");
      setStatusCode(ST_SYNTAX_ERROR_33);
    }
  else if( m_channels[channel] != nullptr )
    {
      Debug_printv("Error: a file is already open on this channel");
      setStatusCode(ST_NO_CHANNEL);
    }
  else
    {
      if ( name[0] == '$' ) 
        name.clear();

      // get file
      MFile *f = m_cwd->cd(mstr::toUTF8(name));

      if( f->isDirectory() )
        {
          if( mode == std::ios_base::in )
            {
              // reading directory
              std::unique_ptr<MFile> entry = std::unique_ptr<MFile>( f->getNextFileInDir() ); 
              if( entry!=nullptr )
                {
                  // regular directory
                  f->rewindDirectory();
                  m_channels[channel] = new iecChannelHandlerDir(this, f);
                  m_numOpenChannels++;
                  m_cwd.reset(MFSOwner::File(f->url));
                  Debug_printv("Reading directory [%s]", f->url.c_str());
                  f = nullptr; // f will be deleted in iecChannelHandlerDir destructor
                  setStatusCode(ST_OK);
                }
              else
                {
                  // can't read file etries => treat directory as file
                  m_cwd.reset(MFSOwner::File(f->url));
                  Debug_printv("Treating directory as file [%s]", f->url.c_str());
                }
            }
          else
            {
              // cannot write to directory
              Debug_printv("Error: attempt to write to directory [%s]", f->url.c_str());
              setStatusCode(ST_WRITE_PROTECT);
              delete f;
              f = nullptr;
            }
        }

      if( f != nullptr )
        {
          // opening a file
          if( (mode == std::ios_base::in) && !f->exists() )
            {
              Debug_printv("Error: file doesn't exist [%s]", f->url.c_str());
              setStatusCode(ST_FILE_NOT_FOUND);
            }
          else if( (mode == std::ios_base::out) && f->exists() && !overwrite )
            {
              Debug_printv("Error: file exists [%s]", f->url.c_str());
              setStatusCode(ST_FILE_EXISTS);
            }
          else
            {
              MStream *new_stream = f->getSourceStream(mode);
              
              if( new_stream==nullptr )
                {
                  Debug_printv("Error: could not get stream for file [%s]", f->url.c_str());
                  setStatusCode(ST_DRIVE_NOT_READY);
                }
              else if( (mode == std::ios_base::in) && new_stream->size()==0 && !f->isDirectory() )
                {
                  Debug_printv("Error: file length is zero [%s]", f->url.c_str());
                  delete new_stream;
                  setStatusCode(ST_FILE_NOT_FOUND);
                }
              else if( !new_stream->isOpen() )
                {
                  Debug_printv("Error: could not open file stream [%s]", f->url.c_str());
                  delete new_stream;
                  setStatusCode(ST_DRIVE_NOT_READY);
                }
              else
                {
                  Debug_printv("Stream created for file [%s]", f->url.c_str());
                  // new_stream will be deleted in iecChannelHandlerFile destructor
                  m_channels[channel] = new iecChannelHandlerFile(this, new_stream, f->isDirectory() ? 0x0801 : -1);
                  m_numOpenChannels++;
                  setStatusCode(ST_OK);
          
                  if( new_stream->has_subdirs )
                    {
                      // Filesystem supports sub directories => set m_cwd to parent directory of file
                      Debug_printv("Subdir Change Directory Here! stream[%s] > base[%s]", f->url.c_str(), f->base().c_str());
                      m_cwd.reset(MFSOwner::File(f->base()));
                    }
                  else
                    {
                      // Handles media files that may have '/' as part of the filename
                      auto f = MFSOwner::File( new_stream->url );
                      Debug_printv( "Change Directory Here! istream[%s] > base[%s]", new_stream->url.c_str(), f->streamFile->url.c_str() );
                      m_cwd.reset( f->streamFile );
                    }
                }
            }
        }
      
      delete f;
    }
}


void iecDrive::close(uint8_t channel)
{
  Debug_printv("iecDrive::close(#%d, %d)", m_devnr, channel);

  if( m_channels[channel] != nullptr )
    {
      delete m_channels[channel];
      m_channels[channel] = nullptr;
      if( m_numOpenChannels>0 ) m_numOpenChannels--;
      Debug_printv("Channel %d closed.", channel);
    }
}


uint8_t iecDrive::write(uint8_t channel, uint8_t *data, uint8_t dataLen) 
{
  iecChannelHandler *handler = m_channels[channel];
  if( handler==nullptr )
    {
      if( m_statusCode==ST_OK ) setStatusCode(ST_FILE_NOT_OPEN);
      return 0;
    }
  else
    return handler->write(data, dataLen);
}

 
uint8_t iecDrive::read(uint8_t channel, uint8_t *data, uint8_t maxDataLen)
{ 
  iecChannelHandler *handler = m_channels[channel];
  if( handler==nullptr )
    {
      if( m_statusCode==ST_OK ) setStatusCode(ST_FILE_NOT_OPEN);
      return 0;
    }
  else
    return handler->read(data, maxDataLen);
}


void iecDrive::execute(const char *cmd, uint8_t cmdLen)
{
  Debug_printv("iecDrive::execute(#%d, \"%s\", %d)", m_devnr, cmd, cmdLen);

  std::string command = std::string(cmd, cmdLen);

  if( mstr::startsWith(command, "S:") )
    {
      // SCRATCH (delete)
      uint8_t n = 0;
      command = command.substr(2);

      MFile *dir = MFSOwner::File(m_cwd->url);
      if( dir!=nullptr )
        {
          if( dir->isDirectory() )
            {
              std::unique_ptr<MFile> entry;
              while( (entry=std::unique_ptr<MFile>(m_cwd->getNextFileInDir()))!=nullptr )
                {
                  if( !entry->isDirectory() && isMatch(mstr::toPETSCII2(entry->name), command) )
                    {
                      Debug_printv("DELETING %s", entry->name.c_str());
                      if( entry->remove() ) n++;
                    }
                  //else Debug_printv("NOT DELETING %s", entry->name.c_str());
                }
            }

          delete dir;
        }

      setStatusCode(ST_SCRATCHED, n);
    }
  else if( mstr::startsWith(command, "CD") )
    {
      set_cwd(mstr::drop(command, 2));
    }
#ifdef SUPPORT_JIFFY
  else if( command=="EJ+" || command=="EJ-" )
    {
      enableJiffyDosSupport(command[2]=='+');
      setStatusCode(ST_OK);
    }
#endif  
#ifdef SUPPORT_EPYX
  else if( command=="EE+" || command=="EE-" )
    {
      enableEpyxFastLoadSupport(command[2]=='+');
      setStatusCode(ST_OK);
    }
#endif  
  else
    {
      setStatusCode(ST_SYNTAX_ERROR_31);
      Debug_printv("Invalid command");
    }
}


void iecDrive::setStatusCode(uint8_t code, uint8_t trk)
{
  m_statusCode = code;
  m_statusTrk  = trk;

  // clear current status buffer to force a call to getStatus()
  clearStatus();
}


bool iecDrive::hasError()
{
  return (m_statusCode>=20) && (m_statusCode!=ST_SPLASH);
}


void iecDrive::getStatus(char *buffer, uint8_t bufferSize)
{
  Debug_printv("iecDrive::getStatus(#%d)", m_devnr);

  const char *msg = NULL;
  switch( m_statusCode )
    {
    case ST_OK             : msg = " OK"; break;
    case ST_SCRATCHED      : msg = "FILES SCRATCHED"; break;
    case ST_WRITE_ERROR    : msg = "WRITE ERROR"; break;
    case ST_WRITE_PROTECT  : msg = "WRITE PROTECT"; break;
    case ST_SYNTAX_ERROR_31:
    case ST_SYNTAX_ERROR_33: msg = "SYNTAX ERROR"; break;
    case ST_FILE_NOT_FOUND : msg = "FILE NOT FOUND"; break;
    case ST_FILE_NOT_OPEN  : msg = "FILE NOT OPEN"; break;
    case ST_FILE_EXISTS    : msg = "FILE EXISTS"; break;
    case ST_SPLASH         : msg = PRODUCT_ID; break;
    case ST_NO_CHANNEL     : msg = "NO CHANNEL"; break;
    case ST_DRIVE_NOT_READY: msg = "DRIVE NOT READY"; break;
    case ST_FILE_TYPE_MISMATCH: msg = "FILE TYPE MISMATCH"; break;
    default                : msg = "UNKNOWN ERROR"; break;
    }

  snprintf(buffer, bufferSize, "%02d,%s,%02d,00\r", m_statusCode, msg, m_statusTrk);

  Debug_printv("status: %s", buffer);
  m_statusCode = ST_OK;
  m_statusTrk  = 0;
}



void iecDrive::reset()
{
  Debug_printv("iecDrive::reset(#%d)", m_devnr);
  setStatusCode(ST_SPLASH);

  // close all open channels
  for(int i=0; i<16; i++)
    if( m_channels[i]!=nullptr )
      close(i);
  m_numOpenChannels = 0;

  IECFileDevice::reset();
}


void iecDrive::set_cwd(std::string path)
{
    // Isolate path
    if ( mstr::startsWith(path, ":") || mstr::startsWith(path, " " ) )
      path = mstr::drop(path, 1);

    Debug_printv("path[%s]", path.c_str());
    path = mstr::toUTF8( path );

    auto n = m_cwd->cd( path );
    if( n != nullptr )
      {
        if( n->exists() && (n->isDirectory() || n->getSourceStream()!=nullptr) )
          {
            m_cwd.reset( n );
            setStatusCode(ST_OK);
          }
        else
          {
            Debug_printv("invalid directory");
            setStatusCode(ST_FILE_NOT_FOUND);
            delete n;
          }
      }
    else
      setStatusCode(ST_SYNTAX_ERROR_31);
}


/* Mount Disk
   We determine the type of image based on the filename extension.
   If the disk_type value passed is not MEDIATYPE_UNKNOWN then that's used instead.
   Return value is MEDIATYPE_UNKNOWN in case of failure.
*/
mediatype_t iecDrive::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
  Debug_printv("filename[%s], disksize[%d] disktype[%d]", filename, disksize, disk_type);
  std::string url = this->m_host->get_basepath();
  
  mstr::toLower(url);
  if ( url == "sd" )
    url = "//sd";
  url += filename;
  
  Debug_printv("DRIVE[#%d] URL[%s] MOUNT[%s]\n", m_devnr, url.c_str(), filename);
  
  m_cwd.reset( MFSOwner::File( url ) );
  
  return MediaType::discover_mediatype(filename); // MEDIATYPE_UNKNOWN
}


// Unmount disk file
void iecDrive::unmount()
{
    Debug_printv("DRIVE[#%d] UNMOUNT\r\n", m_devnr);

    if (m_cwd != nullptr)
    {
      //m_cwd->unmount();
      device_active = false;
    }
}


#endif /* BUILD_IEC */
