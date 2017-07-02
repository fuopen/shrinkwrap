#ifndef SHRINKWRAP_ZSTD_HPP
#define SHRINKWRAP_ZSTD_HPP

#ifndef ZSTD_STATIC_LINKING_ONLY
#define ZSTD_STATIC_LINKING_ONLY
#endif

#include <zstd.h>

#include <streambuf>
#include <stdio.h>
#include <vector>

namespace shrinkwrap
{
  namespace zstd
  {
    class ibuf : public std::streambuf
    {
    public:
      ibuf(FILE* fp)
        :
        strm_(ZSTD_createDStream()),
        input_({0}),
        compressed_buffer_(ZSTD_DStreamInSize()),
        decompressed_buffer_(ZSTD_DStreamOutSize()),
        current_block_position_(0),
        fp_(fp)
      {
        if (fp_)
        {
          res_ = ZSTD_initDStream(strm_); // 16 for GZIP only.
          if (ZSTD_isError(res_))
          {
            // TODO: handle error.
          }
        }
        char* end = ((char*) decompressed_buffer_.data()) + decompressed_buffer_.size();
        setg(end, end, end);
      }

      ibuf(const std::string& file_path) : ibuf(fopen(file_path.c_str(), "rb")) {}

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
          ZSTD_freeDStream(strm_);
          fclose(fp_);
        }
      }

      void move(ibuf&& src)
      {
        strm_ = src.strm_;
        src.strm_ = nullptr;
        compressed_buffer_ = std::move(src.compressed_buffer_);
        decompressed_buffer_ = std::move(src.decompressed_buffer_);

        current_block_position_ = src.current_block_position_;
        fp_ = src.fp_;
        src.fp_ = nullptr;
        res_ = src.res_;
      }

      void replenish_compressed_buffer()
      {
         input_ = {compressed_buffer_.data(), fread(compressed_buffer_.data(), 1, compressed_buffer_.size(), fp_), 0 };
      }

    protected:

      virtual std::streambuf::int_type underflow()
      {
        if (!fp_)
          return traits_type::eof();
        if (gptr() < egptr()) // buffer not exhausted
          return traits_type::to_int_type(*gptr());

        if (!ZSTD_isError(res_))
        {
          if (input_.pos == input_.size && !feof(fp_))
          {
            replenish_compressed_buffer();
          }

          if (res_ == 0 && input_.pos < input_.size)
          {
            res_ = ZSTD_resetDStream(strm_);
            current_block_position_ = std::size_t(ftell(fp_)) - (input_.size - input_.pos);
          }

          ZSTD_outBuffer output = {decompressed_buffer_.data(), decompressed_buffer_.size(), 0};
          res_ = ZSTD_decompressStream(strm_, &output , &input_);

          if (!ZSTD_isError(res_))
          {
            char* start = ((char*) decompressed_buffer_.data());
            setg(start, start, start + output.pos);
          }
        }

        if (gptr() >= egptr())
          return traits_type::eof();
        else if (ZSTD_isError(res_))
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
      ZSTD_DStream* strm_;
      ZSTD_inBuffer input_;
      FILE* fp_;
      std::size_t res_;
      std::size_t current_block_position_;
    };

    class obuf : public std::streambuf
    {
    public:
      obuf(FILE* fp)
        :
        strm_(ZSTD_createCStream()),
        fp_(fp),
        compressed_buffer_(ZSTD_CStreamOutSize()),
        decompressed_buffer_(ZSTD_CStreamInSize()),
        res_(0)
      {
        if (!fp_)
        {
          char* end = ((char*) decompressed_buffer_.data()) + decompressed_buffer_.size();
          setp(end, end);
        }
        else
        {
          res_ = ZSTD_initCStream(strm_, 3);
          if (ZSTD_isError(res_))
          {
            // TODO: handle error.
          }

          char* end = ((char*) decompressed_buffer_.data()) + decompressed_buffer_.size();
          setp((char*) decompressed_buffer_.data(), end);
        }
      }

      obuf(const std::string& file_path) : obuf(fopen(file_path.c_str(), "wb")) {}

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

      virtual ~obuf()
      {
        this->close();
      }

    private:
      void move(obuf&& src)
      {
        compressed_buffer_ = std::move(src.compressed_buffer_);
        decompressed_buffer_ = std::move(src.decompressed_buffer_);
        strm_ = src.strm_;
        fp_ = src.fp_;
        src.fp_ = nullptr;
        res_ = src.res_;
      }

      void close()
      {
        if (fp_)
        {
          sync();
          res_ = ZSTD_freeCStream(strm_);
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
          ZSTD_inBuffer input = {decompressed_buffer_.data(), decompressed_buffer_.size(), 0};
          while (!ZSTD_isError(res_) && input.pos < input.size)
          {
            ZSTD_outBuffer output = {compressed_buffer_.data(), compressed_buffer_.size(), 0};
            res_ = ZSTD_compressStream(strm_, &output, &input);

            if (output.pos && !fwrite(compressed_buffer_.data(), output.pos, 1, fp_))
            {
              // TODO: handle error.
              return traits_type::eof();
            }
          }

          decompressed_buffer_[0] = reinterpret_cast<unsigned char&>(c);
          setp((char*) decompressed_buffer_.data() + 1, (char*) decompressed_buffer_.data() + decompressed_buffer_.size());
        }

        return (!ZSTD_isError(res_) ? traits_type::to_int_type(c) : traits_type::eof());
      }

      virtual int sync()
      {
        if (!fp_)
          return -1;

        ZSTD_inBuffer input = {decompressed_buffer_.data(), (decompressed_buffer_.size() - (epptr() - pptr())), 0};

        if (input.pos < input.size)
        {
          while (!ZSTD_isError(res_) && input.pos < input.size)
          {
            ZSTD_outBuffer output = {compressed_buffer_.data(), compressed_buffer_.size(), 0};
            res_ = ZSTD_compressStream(strm_, &output, &input);

            if (output.pos && !fwrite(compressed_buffer_.data(), output.pos, 1, fp_))
            {
              // TODO: handle error.
              return -1;
            }
          }

          while (!ZSTD_isError(res_) && res_ != 0)
          {
            ZSTD_outBuffer output = {compressed_buffer_.data(), compressed_buffer_.size(), 0};
            res_ = ZSTD_endStream(strm_, &output);
            if (output.pos && !fwrite(compressed_buffer_.data(), output.pos, 1, fp_))
            {
              // TODO: handle error.
              return -1;
            }
          }

          if (ZSTD_isError(res_))
            return -1;

          res_ = ZSTD_resetCStream(strm_, 0);

          setp((char*) decompressed_buffer_.data(), (char*) decompressed_buffer_.data() + decompressed_buffer_.size());
        }

        return 0;
      }

    private:
      std::vector<std::uint8_t> compressed_buffer_;
      std::vector<std::uint8_t> decompressed_buffer_;
      ZSTD_CStream* strm_;
      FILE* fp_;
      std::size_t res_;
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

    private:
      ::shrinkwrap::zstd::ibuf sbuf_;
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

    private:
      ::shrinkwrap::zstd::obuf sbuf_;
    };
  }
}

#endif //SHRINKWRAP_ZSTD_HPP