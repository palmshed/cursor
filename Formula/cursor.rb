class Cursor < Formula
  desc "Professional AI agent with command execution and file operations"
  homepage "https://github.com/bniladridas/cursor"
  url "https://github.com/bniladridas/cursor/archive/refs/tags/v0.1.7.tar.gz"
  sha256 "3bd73b2bde8ae719f84e0027b4e01588e6cdc786556a68c5db9aaaf9a8a9dfe1"
  license "Apache-2.0"

  depends_on "cmake" => :build
  depends_on "cpr"
  depends_on "nlohmann-json"

  def install
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build"
    bin.install "build/bin/cursor-agent" => "cursor"

    # Install configuration template
    (etc/"cursor").install ".env.example" => "config.env"
  end

  def post_install
    (var/"cursor").mkpath
    unless (etc/"cursor/.env").exist?
      cp etc/"cursor/config.env", etc/"cursor/.env"
    end
  end

  test do
    system "#{bin}/cursor", "--version"
  end

  def caveats
    <<~EOS
      Configuration file is located at:
        #{etc}/cursor/.env

      Edit this file with your API keys:
        - TOGETHER_API_KEY (for online mode)
        - CEREBRAS_API_KEY (for Cerebras mode)
        - SERPAPI_KEY (for web search)

      Data directory:
        #{var}/cursor/
    EOS
  end
end
