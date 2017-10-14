#ifndef SHRINKWRAP_GZ_HPP
#define SHRINKWRAP_GZ_HPP

#include <streambuf>
#include <array>
#include <vector>
#include <stdio.h>
#include <zlib.h>
#include <assert.h>
#include <iostream>
#include <limits>
#include <cstring>

namespace shrinkwrap
{
  namespace gz
  {
    class ibuf : public std::streambuf
    {
    public:
      ibuf(FILE* fp)
        :
        zstrm_({0}),
        compressed_buffer_(default_block_size),
        decompressed_buffer_(default_block_size),
        discard_amount_(0),
        current_block_position_(0),
        uncompressed_block_offset_(0),
        fp_(fp),
        put_back_size_(0),
        at_block_boundary_(false)
      {
        if (fp_)
        {
          zlib_res_ = inflateInit2(&zstrm_, 15 + 16); // 16 for GZIP only.
          if (zlib_res_ != Z_OK)
          {
            // TODO: handle error.
          }
        }
        char* end = ((char*) decompressed_buffer_.data()) + decompressed_buffer_.size();
        setg(end, end, end);
      }

      ibuf(const std::string& file_path) : ibuf(fopen(file_path.c_str(), "rb")) {}
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ > 4
      ibuf(ibuf&& src)
        :
        std::streambuf(std::move(src))
      {
        this->move(std::move(src));
      }

      ibuf& operator=(ibuf&& src)
      {
        if (&src != this)
        {
          std::streambuf::operator=(std::move(src));
          this->destroy();
          this->move(std::move(src));
        }

        return *this;
      }
#endif
      virtual ~ibuf()
      {
        this->destroy();
      }

    private:
      //ixzbuf(const ixzbuf& src) = delete;
      //ixzbuf& operator=(const ixzbuf& src) = delete;

      void destroy()
      {
        if (fp_)
        {
          inflateEnd(&zstrm_);
          fclose(fp_);
        }
      }

      void move(ibuf&& src)
      {
        zstrm_ = src.zstrm_;
        src.zstrm_ = {0};
        compressed_buffer_ = std::move(src.compressed_buffer_);
        decompressed_buffer_ = std::move(src.decompressed_buffer_);
        discard_amount_ = src.discard_amount_;
        current_block_position_ = src.current_block_position_;
        uncompressed_block_offset_ = src.uncompressed_block_offset_;
        at_block_boundary_ = src.at_block_boundary_;
        fp_ = src.fp_;
        src.fp_ = nullptr;
        put_back_size_ = src.put_back_size_;
        zlib_res_ = src.zlib_res_;
      }

      void replenish_compressed_buffer()
      {
        zstrm_.next_in = compressed_buffer_.data();
        zstrm_.avail_in = fread(compressed_buffer_.data(), 1, compressed_buffer_.size(), fp_);
      }

    protected:

      virtual std::streambuf::int_type underflow()
      {
        if (!fp_)
          return traits_type::eof();
        if (gptr() < egptr()) // buffer not exhausted
          return traits_type::to_int_type(*gptr());

        while ((zlib_res_ == Z_OK || zlib_res_ == Z_STREAM_END) && gptr() >= egptr() && (zstrm_.avail_in > 0 || !feof(fp_)))
        {
          zstrm_.next_out = decompressed_buffer_.data();
          zstrm_.avail_out = static_cast<std::uint32_t>(decompressed_buffer_.size());

          if (zstrm_.avail_in == 0 && !feof(fp_))
          {
            replenish_compressed_buffer();
          }



          //assert(zstrm_.avail_in > 0);
          if (zlib_res_ == Z_STREAM_END && zstrm_.avail_in > 0)
          {
            zlib_res_ = inflateReset(&zstrm_);
            uncompressed_block_offset_ = 0;
            current_block_position_ = std::size_t(ftell(fp_)) - zstrm_.avail_in;
          }

          zlib_res_ = inflate(&zstrm_, Z_NO_FLUSH);


          char* start = ((char*) decompressed_buffer_.data());
          setg(start, start, start + (decompressed_buffer_.size() - zstrm_.avail_out));
          uncompressed_block_offset_ += (egptr() - gptr());

          if (discard_amount_ > 0)
          {
            std::uint64_t advance_amount = discard_amount_;
            if ((egptr() - gptr()) < advance_amount)
              advance_amount = (egptr() - gptr());
            setg(start, gptr() + advance_amount, egptr());
            discard_amount_ -= advance_amount;
          }
        }

        if (zlib_res_ != Z_OK && zlib_res_ != Z_STREAM_END)
          return traits_type::eof();
        else if (gptr() >= egptr())
          return traits_type::eof();

        return traits_type::to_int_type(*gptr());
      }

      virtual std::streambuf::pos_type seekoff(std::streambuf::off_type off, std::ios_base::seekdir way, std::ios_base::openmode which)
      {
        return pos_type(off_type(-1));
      }

    private:
      std::vector<std::uint8_t> compressed_buffer_;
      std::vector<std::uint8_t> decompressed_buffer_;
      std::size_t put_back_size_;
      bool at_block_boundary_;
    protected:
      static const std::size_t default_block_size = 64 * 1024;
      int zlib_res_;
      z_stream zstrm_;
      std::uint16_t discard_amount_;
      std::size_t current_block_position_;
      std::size_t uncompressed_block_offset_;
      FILE* fp_;
    };

    class obuf : public std::streambuf
    {
    public:
      obuf(FILE* fp)
        :
        zstrm_({0}),
        fp_(fp),
        compressed_buffer_(default_block_size),
        decompressed_buffer_(default_block_size)
      {
        if (!fp_)
        {
          char* end = ((char*) decompressed_buffer_.data()) + decompressed_buffer_.size();
          setp(end, end);
        }
        else
        {
          zlib_res_ = deflateInit2(&zstrm_, Z_DEFAULT_COMPRESSION, Z_DEFLATED, (15 | 16), 8, Z_DEFAULT_STRATEGY); // |16 for GZIP
          if (zlib_res_ != Z_OK)
          {
            // TODO: handle error.
          }

          zstrm_.next_out = compressed_buffer_.data();
          zstrm_.avail_out = static_cast<std::uint32_t>(compressed_buffer_.size());

          char* end = ((char*) decompressed_buffer_.data()) + decompressed_buffer_.size();
          setp((char*) decompressed_buffer_.data(), end);
        }
      }

      obuf(const std::string& file_path) : obuf(fopen(file_path.c_str(), "wb")) {}
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ > 4
      obuf(obuf&& src)
        :
        std::streambuf(std::move(src))
      {
        this->move(std::move(src));
      }

      obuf& operator=(obuf&& src)
      {
        if (&src != this)
        {
          std::streambuf::operator=(std::move(src));
          this->close();
          this->move(std::move(src));
        }

        return *this;
      }
#endif
      virtual ~obuf()
      {
        this->close();
      }

    private:
      void move(obuf&& src)
      {
        compressed_buffer_ = std::move(src.compressed_buffer_);
        decompressed_buffer_ = std::move(src.decompressed_buffer_);
        zstrm_ = src.zstrm_;
        fp_ = src.fp_;
        src.fp_ = nullptr;
        zlib_res_ = src.zlib_res_;
      }

      void close()
      {
        if (fp_)
        {
          sync();
          int res = deflateEnd(&zstrm_);
          if (zlib_res_ == Z_OK)
            zlib_res_ = res;
          fclose(fp_);
          fp_ = nullptr;
        }
      }
    protected:
      virtual int overflow(int c)
      {
        if (!fp_)
          return traits_type::eof();

        if ((epptr() - pptr()) > 0)
        {
          assert(!"Put buffer not empty, this should never happen");
          this->sputc(static_cast<char>(0xFF & c));
        }
        else
        {
          zstrm_.next_in = decompressed_buffer_.data();
          zstrm_.avail_in = static_cast<std::uint32_t>(decompressed_buffer_.size());
          while (zlib_res_ == Z_OK && zstrm_.avail_in > 0)
          {
            zlib_res_ = deflate(&zstrm_, Z_SYNC_FLUSH);

            if ((compressed_buffer_.size() - zstrm_.avail_out) > 0 && !fwrite(compressed_buffer_.data(), compressed_buffer_.size() - zstrm_.avail_out, 1, fp_))
            {
              // TODO: handle error.
              return traits_type::eof();
            }
            zstrm_.next_out = compressed_buffer_.data();
            zstrm_.avail_out = static_cast<std::uint32_t>(compressed_buffer_.size());
          }

          if (zlib_res_ == Z_STREAM_END)
            zlib_res_ = deflateReset(&zstrm_);

          assert(zstrm_.avail_in == 0);
          decompressed_buffer_[0] = reinterpret_cast<unsigned char&>(c);
          setp((char*) decompressed_buffer_.data() + 1, (char*) decompressed_buffer_.data() + decompressed_buffer_.size());
        }

        return (zlib_res_ == Z_OK ? traits_type::to_int_type(c) : traits_type::eof());
      }

      virtual int sync()
      {
        if (!fp_)
          return -1;

        zstrm_.next_in = decompressed_buffer_.data();
        zstrm_.avail_in = static_cast<std::uint32_t>(decompressed_buffer_.size() - (epptr() - pptr()));
        if (zstrm_.avail_in)
        {
          while (zlib_res_ == Z_OK && zstrm_.avail_in > 0)
          {
            zlib_res_ = deflate(&zstrm_, Z_SYNC_FLUSH);

            if ((compressed_buffer_.size() - zstrm_.avail_out) > 0 && !fwrite(compressed_buffer_.data(), compressed_buffer_.size() - zstrm_.avail_out, 1, fp_))
            {
              // TODO: handle error.
              return -1;
            }
            zstrm_.next_out = compressed_buffer_.data();
            zstrm_.avail_out = static_cast<std::uint32_t>(compressed_buffer_.size());

          }

          if (zlib_res_ != Z_OK)
            return -1;

          assert(zstrm_.avail_in == 0);
          setp((char*) decompressed_buffer_.data(), (char*) decompressed_buffer_.data() + decompressed_buffer_.size());
        }

        return 0;
      }

    private:
      static const std::size_t default_block_size = 64 * 1024;
      std::vector<std::uint8_t> compressed_buffer_;
      std::vector<std::uint8_t> decompressed_buffer_;
      z_stream zstrm_;
      FILE* fp_;
      int zlib_res_;
    };

    class istream : public std::istream
    {
    public:
      istream(const std::string& file_path)
        :
        std::istream(&sbuf_),
        sbuf_(file_path)
      {
      }
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ > 4
      istream(istream&& src)
        :
        std::istream(&sbuf_),
        sbuf_(std::move(src.sbuf_))
      {
      }

      istream& operator=(istream&& src)
      {
        if (&src != this)
        {
          std::istream::operator=(std::move(src));
          sbuf_ = std::move(src.sbuf_);
        }
        return *this;
      }
#endif
    private:
      ::shrinkwrap::gz::ibuf sbuf_;
    };



    class ostream : public std::ostream
    {
    public:
      ostream(const std::string& file_path)
        :
        std::ostream(&sbuf_),
        sbuf_(file_path)
      {
      }
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ > 4
      ostream(ostream&& src)
        :
        std::ostream(&sbuf_),
        sbuf_(std::move(src.sbuf_))
      {
      }

      ostream& operator=(ostream&& src)
      {
        if (&src != this)
        {
          std::ostream::operator=(std::move(src));
          sbuf_ = std::move(src.sbuf_);
        }
        return *this;
      }
#endif
    private:
      ::shrinkwrap::gz::obuf sbuf_;
    };
  }

  namespace bgz
  {
    class ibuf : public gz::ibuf
    {
    public:
      using gz::ibuf::ibuf;
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ > 4
      ibuf(ibuf&& src)
        :
        gz::ibuf(std::move(src))
      {
      }

      ibuf& operator=(ibuf&& src)
      {
        if (&src != this)
        {
          ibuf::operator=(std::move(src));
        }

        return *this;
      }
#endif

      virtual ~ibuf()
      {
      }

    protected:
      virtual std::streambuf::pos_type seekoff(std::streambuf::off_type off, std::ios_base::seekdir way, std::ios_base::openmode which) // Supports tellg for virtual offset.
      {
        if (off == 0 && way == std::ios::cur)
        {
          if (egptr() - gptr() == 0 && zlib_res_ == Z_STREAM_END)
          {
            std::uint64_t compressed_offset = std::size_t(ftell(fp_)) - zstrm_.avail_in;
            std::uint16_t uncompressed_offset = 0;
            std::uint64_t virtual_offset = ((compressed_offset << 16) | uncompressed_offset);
            return pos_type(off_type(virtual_offset));
          }
          else
          {
            std::uint64_t compressed_offset = current_block_position_;
            std::uint16_t uncompressed_offset = (std::uint16_t(uncompressed_block_offset_) - std::uint16_t(egptr() - gptr())) + discard_amount_;
            std::uint64_t virtual_offset = ((compressed_offset << 16) | uncompressed_offset);
            return pos_type(off_type(virtual_offset));
          }
        }
        return pos_type(off_type(-1));
      }

      //coffset << 16 | uoffset
      virtual std::streambuf::pos_type seekpos(std::streambuf::pos_type pos, std::ios_base::openmode which)
      {
        std::uint64_t compressed_offset = ((static_cast<std::uint64_t>(pos) >> 16) & 0x0000FFFFFFFFFFFF);
        std::uint16_t uncompressed_offset = (std::uint16_t) (static_cast<std::uint64_t>(pos) & 0x000000000000FFFF);

        if (fp_ == 0 || sync())
          return pos_type(off_type(-1));

        long seek_amount = static_cast<long>(compressed_offset);
        if (fseek(fp_, seek_amount, SEEK_SET))
          return pos_type(off_type(-1));

        current_block_position_ = compressed_offset;
        discard_amount_ = uncompressed_offset;

        zstrm_.next_in = nullptr;
        zstrm_.avail_in = 0;
        zlib_res_ = inflateReset(&zstrm_);
        char* end = egptr();
        setg(end, end, end);

        return pos;
      }
    };

    class obuf : public std::streambuf
    {
    public:
      obuf(FILE* fp)
        :
        zstrm_({0}),
        fp_(fp),
        compressed_buffer_(default_block_size),
        decompressed_buffer_(default_block_size)
      {
        if (!fp_)
        {
          char* end = ((char*) decompressed_buffer_.data()) + decompressed_buffer_.size();
          setp(end, end);
        }
        else
        {
          zlib_res_ = deflateInit2(&zstrm_, Z_DEFAULT_COMPRESSION, Z_DEFLATED, (15 | 16), 8, Z_DEFAULT_STRATEGY); // |16 for GZIP
          if (zlib_res_ != Z_OK)
          {
            // TODO: handle error.
          }

          zstrm_.next_out = compressed_buffer_.data();
          zstrm_.avail_out = static_cast<std::uint32_t>(compressed_buffer_.size());

          char* end = ((char*) decompressed_buffer_.data()) + decompressed_buffer_.size();
          setp((char*) decompressed_buffer_.data(), end);
        }
      }

      obuf(const std::string& file_path) : obuf(fopen(file_path.c_str(), "wb")) {}
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ > 4
      obuf(obuf&& src)
        :
        std::streambuf(std::move(src))
      {
        this->move(std::move(src));
      }

      obuf& operator=(obuf&& src)
      {
        if (&src != this)
        {
          std::streambuf::operator=(std::move(src));
          this->close();
          this->move(std::move(src));
        }

        return *this;
      }
#endif
      virtual ~obuf()
      {
        this->close();
      }

    private:
      void move(obuf&& src)
      {
        compressed_buffer_ = std::move(src.compressed_buffer_);
        decompressed_buffer_ = std::move(src.decompressed_buffer_);
        zstrm_ = src.zstrm_;
        fp_ = src.fp_;
        src.fp_ = nullptr;
        zlib_res_ = src.zlib_res_;
      }

      void close()
      {
        if (fp_)
        {
          sync();
          int res = deflateEnd(&zstrm_);
          if (zlib_res_ == Z_OK)
            zlib_res_ = res;
          fclose(fp_);
          fp_ = nullptr;
        }
      }
    protected:
      virtual int overflow(int c)
      {
        if (!fp_)
          return traits_type::eof();

        if ((epptr() - pptr()) > 0)
        {
          assert(!"Put buffer not empty, this should never happen");
          this->sputc(static_cast<char>(0xFF & c));
        }
        else
        {
          zstrm_.next_in = decompressed_buffer_.data();
          zstrm_.avail_in = static_cast<std::uint32_t>(decompressed_buffer_.size());
          while (zlib_res_ == Z_OK)
          {
            zlib_res_ = deflate(&zstrm_, Z_FINISH);

            if (!fwrite(compressed_buffer_.data(), compressed_buffer_.size() - zstrm_.avail_out, 1, fp_))
            {
              // TODO: handle error.
              return traits_type::eof();
            }
            zstrm_.next_out = compressed_buffer_.data();
            zstrm_.avail_out = static_cast<std::uint32_t>(compressed_buffer_.size());

          }

          if (zlib_res_ == Z_STREAM_END)
            zlib_res_ = deflateReset(&zstrm_);

          assert(zstrm_.avail_in == 0);
          decompressed_buffer_[0] = reinterpret_cast<unsigned char&>(c);
          setp((char*) decompressed_buffer_.data() + 1, (char*) decompressed_buffer_.data() + decompressed_buffer_.size());
        }

        return (zlib_res_ == Z_OK ? traits_type::to_int_type(c) : traits_type::eof());
      }

      virtual int sync()
      {
        if (!fp_)
          return -1;

        zstrm_.next_in = decompressed_buffer_.data();
        zstrm_.avail_in = static_cast<std::uint32_t>(decompressed_buffer_.size() - (epptr() - pptr()));
        if (zstrm_.avail_in)
        {
          while (zlib_res_ == Z_OK)
          {
            zlib_res_ = deflate(&zstrm_, Z_FINISH);

            if (!fwrite(compressed_buffer_.data(), compressed_buffer_.size() - zstrm_.avail_out, 1, fp_))
            {
              // TODO: handle error.
              return -1;
            }
            zstrm_.next_out = compressed_buffer_.data();
            zstrm_.avail_out = static_cast<std::uint32_t>(compressed_buffer_.size());

          }

          if (zlib_res_ == Z_STREAM_END)
            zlib_res_ = deflateReset(&zstrm_);

          if (zlib_res_ != Z_OK)
            return -1;

          assert(zstrm_.avail_in == 0);
          setp((char*) decompressed_buffer_.data(), (char*) decompressed_buffer_.data() + decompressed_buffer_.size());
        }

        return 0;
      }

    private:
      static const std::size_t default_block_size = 64 * 1024;
      std::vector<std::uint8_t> compressed_buffer_;
      std::vector<std::uint8_t> decompressed_buffer_;
      z_stream zstrm_;
      FILE* fp_;
      int zlib_res_;
    };

    class istream : public std::istream
    {
    public:
      istream(const std::string& file_path)
        :
        std::istream(&sbuf_),
        sbuf_(file_path)
      {
      }
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ > 4
      istream(istream&& src)
        :
        std::istream(&sbuf_),
        sbuf_(std::move(src.sbuf_))
      {
      }

      istream& operator=(istream&& src)
      {
        if (&src != this)
        {
          std::istream::operator=(std::move(src));
          sbuf_ = std::move(src.sbuf_);
        }
        return *this;
      }
#endif
    private:
      ::shrinkwrap::bgz::ibuf sbuf_;
    };

    class ostream : public std::ostream
    {
    public:
      ostream(const std::string& file_path)
        :
        std::ostream(&sbuf_),
        sbuf_(file_path)
      {
      }
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ > 4
      ostream(ostream&& src)
        :
        std::ostream(&sbuf_),
        sbuf_(std::move(src.sbuf_))
      {
      }

      ostream& operator=(ostream&& src)
      {
        if (&src != this)
        {
          std::ostream::operator=(std::move(src));
          sbuf_ = std::move(src.sbuf_);
        }
        return *this;
      }
#endif
    private:
      ::shrinkwrap::bgz::obuf sbuf_;
    };
  }
}

#endif //SHRINKWRAP_GZ_HPP