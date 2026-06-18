class Cursor < Formula
  desc "Professional AI agent with command execution and file operations"
  homepage "https://github.com/harpertoken/cursor"
  url "https://github.com/harpertoken/cursor/archive/v0.1.tar.gz"
  sha256 "d5558cd419c8d46bdc958064cb97f963d1ea793866414c025906ec15033512ed"
  license "Apache-2.0"

  depends_on "cmake" => :build
  depends_on "cpr"
  depends_on "nlohmann-json"

  def install
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build"
    bin.install "build/bin/cursor-agent"

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
    system "#{bin}/cursor-agent", "--version"
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
