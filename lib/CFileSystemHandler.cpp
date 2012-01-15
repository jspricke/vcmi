#include "StdInc.h"
#include "CFileSystemHandler.h"

#include "zlib.h"
#include "vcmi_endian.h"
#include "VCMIDirs.h"

ui8 CMemoryStream::readInt8()
{
	assert(seekPos < length);
	return data[seekPos++];
}

ui16 CMemoryStream::readInt16()
{
	assert(seekPos < length - 1);
	
	seekPos += 2;
	return read_le_u16(data);
}

ui32 CMemoryStream::readInt32()
{
	assert(seekPos < length - 3);

	seekPos += 4;
	return read_le_u32(data);
}

CMemoryStream::CMemoryStream(const std::string & filePath) : data(NULL), seekPos(0), length(0)
{
	std::ifstream fileInput(filePath.c_str(), std::ios::in | std::ios::binary);
	
	if (fileInput.is_open())
	{
		length = fileInput.tellg();
		data = new ui8[length];
		fileInput.read(reinterpret_cast<char *>(data), length);
		fileInput.close();
	}
	else
	{
		tlog1 << "File " << filePath << " doesn't exist." << std::endl;
	}
}

CMemoryStream::CMemoryStream(const CMemoryStream & cpy)
{
	*this = cpy;
}

CMemoryStream & CMemoryStream::operator=(const CMemoryStream & cpy)
{
	data = new ui8[cpy.length];
	memcpy(data, cpy.data, length);
	length = cpy.length;
	seekPos = cpy.seekPos;
	return *this;
}

ui8 * CMemoryStream::getRawData()
{
	return data;
}

void CMemoryStream::writeToFile(const std::string & destFile) const
{
	std::ofstream out(destFile.c_str(),std::ios_base::binary);
	out.write(reinterpret_cast<char *>(data), length);
	out.close();
}

void CMemoryStream::setSeekPos(size_t pos)
{
	assert(pos < length);
	seekPos = pos;
}

inline void CMemoryStream::reset()
{ 
	seekPos = 0; 
}

inline size_t CMemoryStream::getSeekPos() const 
{ 
	return seekPos; 
}

inline size_t CMemoryStream::getLength() const 
{ 
	return length; 
}

inline bool CMemoryStream::moreBytesToRead() const 
{ 
	return seekPos < length; 
}

void IResourceLoader::addEntryToMap(TResourcesMap & map, const std::string & name)
{
	std::pair<std::string, std::string> resData = CFileSystemHandler::adaptResourceName(name);
	EResType::EResType extType = CFileSystemHandler::convertFileExtToResType(resData.second);
	ResourceIdentifier ident(prefix + resData.first, extType);
	ResourceLocator locator(this, name);
	map[ident].push_back(locator);
}

void CLodResourceLoader::insertEntriesIntoResourcesMap(TResourcesMap & map)
{
	// Open LOD file
	std::ifstream LOD;
	LOD.open(archiveFile.c_str(), std::ios::in | std::ios::binary);

	if(!LOD.is_open()) 
	{
		tlog1 << "Cannot open " << archiveFile << std::endl;
		return;
	}

	// Read count of total files
	ui32 temp;
	LOD.seekg(8);
	LOD.read((char *)&temp, 4);
	size_t totalFiles = SDL_SwapLE32(temp);

	// Check if LOD file is empty
	LOD.seekg(0x5c, std::ios::beg);
	if(!LOD)
	{
		tlog2 << archiveFile << " doesn't store anything!\n";
		return;
	}

	// Define LodEntry struct
	struct LodEntryBlock {
		char filename[16];
		ui32 offset;				/* little endian */
		ui32 uncompressedSize;	/* little endian */
		ui32 unused;				/* little endian */
		ui32 size;				/* little endian */
	};

	// Allocate a LodEntry array and load data into it
	struct LodEntryBlock * lodEntries = new struct LodEntryBlock[totalFiles];
	LOD.read(reinterpret_cast<char *>(lodEntries), sizeof(struct LodEntryBlock) * totalFiles);

	// Insert all lod entries to a vector
	for(size_t i = 0; i < totalFiles; i++)
	{
		// Create lod entry with correct name, converted file ext, offset, size,...
		ArchiveEntry entry;
		std::pair<std::string, std::string> lodName = CFileSystemHandler::adaptResourceName(lodEntries[i].filename);
		std::string fileExt = lodName.second;

		entry.name = lodName.first;
		entry.type = CFileSystemHandler::convertFileExtToResType(fileExt);
		entry.offset= SDL_SwapLE32(lodEntries[i].offset);
		entry.realSize = SDL_SwapLE32(lodEntries[i].uncompressedSize);
		entry.size = SDL_SwapLE32(lodEntries[i].size);
		
		// Add lod entry to local entries map
		entries.insert(std::make_pair(lodEntries[i].filename, entry));

		// Add resource locator to global map
		ResourceIdentifier mapIdent(prefix + entry.name, entry.type);
		ResourceLocator locator(this, lodEntries[i].filename);
		
		map[mapIdent].push_back(locator);
	}
	
	// Delete LodEntry array
	delete [] lodEntries;
}

TMemoryStreamPtr CLodResourceLoader::loadResource(const std::string & resourceName)
{
	assert(entries.find(resourceName) == entries.end());

	const ArchiveEntry entry = entries[resourceName];

	ui8 * outp;
	TMemoryStreamPtr rslt;

	std::ifstream LOD;
	LOD.open(archiveFile.c_str(), std::ios::in | std::ios::binary);
	
	//file is not compressed
	if (entry.size == 0) 
	{
		outp = new ui8[entry.realSize];

		LOD.seekg(entry.offset, std::ios::beg);
		LOD.read((char*)outp, entry.realSize);
		rslt = shared_ptr<CMemoryStream>(new CMemoryStream(outp, entry.realSize));
		return rslt;
	}
	//we will decompress file
	else 
	{
		outp = new ui8[entry.size];

		LOD.seekg(entry.offset, std::ios::beg);
		LOD.read((char*)outp, entry.size);
		ui8 * decomp = NULL;
		if (!decompressFile(outp, entry.size, entry.realSize, decomp))
		{
			tlog1 << "File decompression wasn't successful. Resource name: " << resourceName << std::endl;
		}
		delete[] outp;
		
		rslt = shared_ptr<CMemoryStream>(new CMemoryStream(decomp, entry.realSize));
		return rslt;
	}

	return rslt;
}

bool CLodResourceLoader::decompressFile(ui8 * in, int size, int realSize, ui8 *& out, int wBits)
{
	int ret;
	unsigned have;
	z_stream strm;
	out = new ui8 [realSize];
	int latPosOut = 0;

	/* allocate inflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit2(&strm, wBits);
	if (ret != Z_OK)
		return ret;
	int chunkNumber = 0;
	do
	{
		if(size < chunkNumber * LodDecompressHelper::FCHUNK)
			break;
		strm.avail_in = std::min(LodDecompressHelper::FCHUNK, size - chunkNumber * LodDecompressHelper::FCHUNK);
		if (strm.avail_in == 0)
			break;
		strm.next_in = in + chunkNumber * LodDecompressHelper::FCHUNK;

		/* run inflate() on input until output buffer not full */
		do
		{
			strm.avail_out = realSize - latPosOut;
			strm.next_out = out + latPosOut;
			ret = inflate(&strm, Z_NO_FLUSH);
			//assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
			bool breakLoop = false;
			switch (ret)
			{
			case Z_STREAM_END:
				breakLoop = true;
				break;
			case Z_NEED_DICT:
				ret = Z_DATA_ERROR;	 /* and fall through */
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
				(void)inflateEnd(&strm);
				return ret;
			}

			if(breakLoop)
				break;

			have = realSize - latPosOut - strm.avail_out;
			latPosOut += have;
		} while (strm.avail_out == 0);

		++chunkNumber;
		/* done when inflate() says it's done */
	} while (ret != Z_STREAM_END);

	/* clean up and return */
	(void)inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

void CFileResourceLoader::insertEntriesIntoResourcesMap(TResourcesMap & map)
{
	boost::filesystem::recursive_directory_iterator enddir;
	if(boost::filesystem::exists(pathToFolder))
	{
		std::vector<std::string> path;
		for (boost::filesystem::recursive_directory_iterator dir(pathToFolder); dir!=enddir; dir++)
		{
			//If a directory was found - add name to vector to recreate full path later
			if (boost::filesystem::is_directory(dir->status()))
			{
				path.resize(dir.level() + 1);
				path.back() = dir->path().leaf();
			}
			if(boost::filesystem::is_regular(dir->status()))
			{
				//we can't get relative path with boost at the moment - need to create path to file manually
				std::string relativePath, name, ext;
				for (size_t i=0; i<dir.level() && i<path.size(); i++)
					relativePath += path[i] + '/';

				relativePath += dir->path().leaf();
				addEntryToMap(map, relativePath);
			}
		}
	}
	else
	{
		if(!pathToFolder.empty())
			tlog1 << "Warning: No " + pathToFolder + "/ folder!" << std::endl;
	}
}

TMemoryStreamPtr CFileResourceLoader::loadResource(const std::string & resourceName)
{
	TMemoryStreamPtr rslt(new CMemoryStream(resourceName));
	if(rslt->getLength() == 0)
		rslt = shared_ptr<CMemoryStream>();

	return rslt;
}

void CSoundResourceHandler::insertEntriesIntoResourcesMap(TResourcesMap & map)
{
	std::ifstream fileHandle(archiveFile.c_str(), std::ios::in | std::ios::binary);
	if (!fileHandle.good())
	{
		tlog1 << "File " << archiveFile << " couldn't be opened" << std::endl;
		return;
	}

	ui32 temp;
	fileHandle.read((char *)&temp, 4);
	size_t totalFiles = SDL_SwapLE32(temp);

	struct SoundEntryBlock
	{
		char filename[40];
		Uint32 offset;				/* little endian */
		Uint32 size;				/* little endian */
	};

	fileHandle.seekg(4);
	struct SoundEntryBlock * sndEntries = new struct SoundEntryBlock[totalFiles];
	fileHandle.read(reinterpret_cast<char *>(sndEntries), sizeof(struct SoundEntryBlock) * totalFiles);

	for (size_t i = 0; i < totalFiles; i++)
	{
		SoundEntryBlock sndEntry = sndEntries[i];
		ArchiveEntry entry;

		entry.name = sndEntry.filename;
		entry.offset = SDL_SwapLE32(sndEntry.offset);
		entry.size = SDL_SwapLE32(sndEntry.size);

		entries.insert(std::make_pair(entry.name, entry));

		addEntryToMap(map, entry.name);
	}

	delete[] sndEntries;
	fileHandle.close();
}

void CVideoResourceHandler::insertEntriesIntoResourcesMap(TResourcesMap & map)
{
	// Open archive file
	std::ifstream fileHandle(archiveFile.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
	if (!fileHandle.good())
	{
		tlog1 << "File " << archiveFile << " couldn't be opened" << std::endl;
		return;
	}
	
	// Check if file size is greater than 48 bytes
	size_t fileSize = fileHandle.tellg();
	if(fileSize < 48)
	{
		tlog1 << archiveFile << " doesn't contain needed data!\n";
		return;
	}
	fileHandle.seekg(0);
	
	ui32 temp;
	fileHandle.read((char *)&temp, 4);
	size_t totalFiles = SDL_SwapLE32(temp);
	
	// video entry structure in catalog
	struct VideoEntryBlock
	{
		char filename[40];
		Uint32 offset;		/* little endian */
	};
	
	fileHandle.seekg(4);
	struct VideoEntryBlock * vidEntries = new struct VideoEntryBlock[totalFiles];
	fileHandle.read(reinterpret_cast<char *>(vidEntries), sizeof(struct VideoEntryBlock) * totalFiles);

	for(size_t i = 0; i < totalFiles; i++)
	{
		VideoEntryBlock vidEntry = vidEntries[i];
		ArchiveEntry entry;

		entry.name = vidEntry.filename;
		entry.offset = SDL_SwapLE32(vidEntry.offset);

		// There is no size, so check where the next file is
		if (i == totalFiles - 1) 
			entry.size = fileSize - entry.offset;
		else 
		{
			VideoEntryBlock ve_next = vidEntries[i + 1];
			entry.size = SDL_SwapLE32(ve_next.offset) - entry.offset;
		}

		entries.insert(std::make_pair(entry.name, entry));
		
		addEntryToMap(map, entry.name);
	}
}

TMemoryStreamPtr CMediaResourceHandler::loadResource(const std::string & resourceName)
{
	assert(entries.find(resourceName) == entries.end());

	const ArchiveEntry entry = entries[resourceName];

	TMemoryStreamPtr rslt;

	std::ifstream fileHandle;
	fileHandle.open(archiveFile.c_str(), std::ios::in | std::ios::binary);
	if (!fileHandle.good())
	{
		tlog1 << "Archive file " << archiveFile << " is corrupt." << std::endl;
		return rslt;
	}
	
	fileHandle.seekg(entry.offset);
	ui8 * outp = new ui8[entry.size];
	fileHandle.read(reinterpret_cast<char *>(outp), entry.size);
	fileHandle.close();

	rslt = shared_ptr<CMemoryStream>(new CMemoryStream(outp, entry.size));
	return rslt;
}

CFileSystemHandler::CFileSystemHandler()
{
	mutex = new boost::mutex;
}

CFileSystemHandler::~CFileSystemHandler()
{
	for (size_t i = 0; i < loaders.size(); ++i)
		delete loaders[i];

	delete mutex;
}

void CFileSystemHandler::addHandler(IResourceLoader * resHandler)
{
	loaders.push_back(resHandler);
	resHandler->insertEntriesIntoResourcesMap(resources);
}

EResType::EResType CFileSystemHandler::convertFileExtToResType(const std::string & fileExt)
{
	// Create convert map statically once
	using namespace EResType;
	using namespace boost::assign;

	static const std::map<std::string, ::EResType::EResType> extMap = map_list_of("TXT", TEXT)
		(".JSON", TEXT)(".DEF", ANIMATION)(".MSK", MASK)(".MSG", MASK)
		(".H3C", CAMPAIGN)(".H3M", MAP)(".FNT", FONT)(".BMP", GRAPHICS)
		(".JPG", GRAPHICS)(".PCX", GRAPHICS)(".PNG", GRAPHICS)(".TGA", GRAPHICS)
		(".WAV", SOUND)(".SMK", VIDEO)(".BIK", VIDEO);

	// Convert file ext(string) to resource type(enum)
	std::map<std::string, ::EResType::EResType>::const_iterator it = extMap.find(fileExt);
	if(it == extMap.end())
		return OTHER;
	else
		return it->second;
}

std::pair<std::string, std::string> CFileSystemHandler::adaptResourceName(const std::string & resName)
{
	std::string fileNameNew, fileExtNew;

	// Convert fileName to uppercase
	std::transform(resName.begin(), resName.end(), fileNameNew.begin(), toupper);

	// Get position of file extension dot
	size_t dotPos = fileNameNew.find_last_of("/.");

	if(dotPos != std::string::npos && fileNameNew[dotPos] == '.')
	{
		// Set name and ext correctly
		fileExtNew = fileNameNew.substr(dotPos);
		fileNameNew.erase(dotPos);
	}

	return std::make_pair(fileNameNew, fileExtNew);
}


TMemoryStreamPtr CFileSystemHandler::getResource(const ResourceIdentifier & identifier, bool fromBegin /*=false */, bool unpackResource /*=false */)
{
	TMemoryStreamPtr rslt;

	// check if resource is registered
	if(resources.find(identifier) == resources.end())
	{
		tlog2 << "Resource with name " << identifier.name << " and type " 
			<< identifier.type << " wasn't found." << std::endl;
		return rslt;
	}
	
	// get last inserted resource locator (default behavior)
	std::list<ResourceLocator> locators = resources.at(identifier);
	
	// get former/origin resource e.g. from lod with fromBegin=true 
	// and get the latest inserted resource with fromBegin=false
	ResourceLocator loc;
	if (!fromBegin)
		loc = locators.back();
	else 
		loc = locators.front();
	
	// check if resource is already loaded
	if(memoryStreams.find(loc) == memoryStreams.end())
	{
		// load it
		return addResource(loc, unpackResource);
	}
	
	weak_ptr<CMemoryStream> ptr = memoryStreams.at(loc);
	if (ptr.expired())
	{
		// load it
		return addResource(loc, unpackResource);
	}
		
	// already loaded, just return resource
	rslt = shared_ptr<CMemoryStream>(ptr);
	return rslt;
}

TMemoryStreamPtr CFileSystemHandler::addResource(const ResourceLocator & loc, bool unpackResource /*=false */)
{
	mutex->lock();
	TMemoryStreamPtr rslt = loc.loader->loadResource(loc.resourceName);
	
	// Don't register unpacked data, as this would result in trouble, when you first load the same
	// resource packed and then unpacked if it's still in use at the same time.
	// MAPS/CAMPAIGNS don't need to shared anyway as it's impossible to play two campaigns the same time.:)
	if (unpackResource)
		rslt = getUnpackedData(rslt);
	else
	{
		weak_ptr<CMemoryStream> ptr(rslt);
		memoryStreams.insert(std::make_pair(loc, ptr));
	}
	mutex->unlock();
	return rslt;
}

std::string CFileSystemHandler::getResourceAsString(const ResourceIdentifier & identifier, bool fromBegin /*=false */)
{
	TMemoryStreamPtr memStream = getResource(identifier, fromBegin);
	return memStream->getDataAsString();
}

TMemoryStreamPtr CFileSystemHandler::getUnpackedResource(const ResourceIdentifier & identifier, bool fromBegin /*=false */)
{
	return getResource(identifier, fromBegin, true);
}

//It is possible to use uncompress function from zlib but we  need to know decompressed size (not present in compressed data)
TMemoryStreamPtr CFileSystemHandler::getUnpackedData(TMemoryStreamPtr memStream) const
{
	std::string filename = GVCMIDirs.UserPath + "/tmp_gzip";

	FILE * file = fopen(filename.c_str(), "wb");
	fwrite(memStream->getRawData(), 1, memStream->getLength(), file);
	fclose(file);

	TMemoryStreamPtr ret = getUnpackedFile(filename);
	remove(filename.c_str());
	return ret;
}

TMemoryStreamPtr CFileSystemHandler::getUnpackedFile(const std::string & path) const
{
	const int bufsize = 65536;
	int mapsize = 0;

	gzFile map = gzopen(path.c_str(), "rb");
	assert(map);
	std::vector<ui8 *> mapstr;

	// Read a map by chunks
	// We could try to read the map size directly (cf RFC 1952) and then read
	// directly the whole map, but that would create more problems.
	do {
		ui8 *buf = new ui8[bufsize];

		int ret = gzread(map, buf, bufsize);
		if (ret == 0 || ret == -1) {
			delete [] buf;
			break;
		}

		mapstr.push_back(buf);
		mapsize += ret;
	} while(1);

	gzclose(map);

	// Now that we know the uncompressed size, reassemble the chunks
	ui8 *initTable = new ui8[mapsize];

	std::vector<ui8 *>::iterator it;
	int offset;
	int tocopy = mapsize;
	for (it = mapstr.begin(), offset = 0; 
		it != mapstr.end(); 
		it++, offset+=bufsize ) {
			memcpy(&initTable[offset], *it, tocopy > bufsize ? bufsize : tocopy);
			tocopy -= bufsize;
			delete [] *it;
	}
	
	TMemoryStreamPtr rslt(new CMemoryStream(initTable, mapsize));
	return rslt;
}

void CFileSystemHandler::writeMemoryStreamToFile(TMemoryStreamPtr memStream, const std::string & destFile) const
{
	memStream->writeToFile(destFile);
}