class Cursor < Formula
  desc "Professional AI agent with command execution and file operations"
  homepage "https://github.com/bniladridas/cursor"
  url "https://github.com/bniladridas/cursor/archive/refs/tags/v0.1.8.tar.gz"
  sha256 "8f5e29528edc6be08acfc81b0408330a4004da19453984dbc6176799277eecdb"
  license "Apache-2.0"

  depends_on "cmake" => :build
  depends_on "cpr"
  depends_on "nlohmann-json"

  def install
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build"
    bin.install "build/bin/cursor-agent"

    (etc/"cursor-agent").install ".env.example" => "config.env"
  end

  def post_install
    (var/"cursor-agent").mkpath
    unless (etc/"cursor-agent/.env").exist?
      cp etc/"cursor-agent/config.env", etc/"cursor-agent/.env"
    end
  end

  test do
    system "#{bin}/cursor-agent", "--version"
  end

  def caveats
    <<~EOS
      Configuration file is located at:
        #{etc}/cursor-agent/.env

      Edit this file with your API keys:
        - TOGETHER_API_KEY (for online mode)
        - CEREBRAS_API_KEY (for Cerebras mode)
        - SERPAPI_KEY (for web search)

      Data directory:
        #{var}/cursor-agent/
    EOS
  end
end
