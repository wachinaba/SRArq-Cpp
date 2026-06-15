#ifndef __SRARQ_H__
#define __SRARQ_H__

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace srarq {

namespace details {
struct EmptyLogger {
  static void log(const std::string& message) {}
};

// Profiling counters for root-cause analysis (shared across all SRArqSender
// instances)
struct TimeoutProfiler {
  static void record_timeout() {
    // Use function-local statics to avoid multiple definition issues
    static uint32_t timeout_count_total = 0;
    static uint32_t timeout_count_last_second = 0;
    static uint32_t timeout_max_burst_in_tick = 0;
    static uint32_t timeout_current_tick_burst = 0;
    static uint32_t last_tick_ms = 0;
    static uint32_t last_log_ms = 0;

    timeout_count_total++;
    timeout_count_last_second++;

    // Track burst in same tick (1ms resolution)
    uint32_t now_ms = 0;
#ifdef ARDUINO
    now_ms = millis();
#else
    // Fallback if millis() not available
    now_ms = timeout_count_total;  // Approximate
#endif

    if (now_ms != last_tick_ms) {
      // New tick - reset burst counter
      if (timeout_current_tick_burst > timeout_max_burst_in_tick) {
        timeout_max_burst_in_tick = timeout_current_tick_burst;
      }
      timeout_current_tick_burst = 1;
      last_tick_ms = now_ms;
    } else {
      timeout_current_tick_burst++;
    }

    // Log periodically (every 5 seconds) or on burst
    if (now_ms - last_log_ms > 5000 || timeout_current_tick_burst > 10) {
#ifdef ARDUINO
      Serial.printf(
          "SRArqTimeoutProfiler: total=%u, last_sec=%u, max_burst=%u, "
          "current_burst=%u\n",
          timeout_count_total, timeout_count_last_second,
          timeout_max_burst_in_tick, timeout_current_tick_burst);
      Serial.flush();
#endif
      timeout_count_last_second = 0;
      last_log_ms = now_ms;
    }
  }
};

/*
Mutex {
  static MutexHandleType create();
  static void lock(MutexHandleType handle);
}

*/
}  // namespace details

/**
 * @brief シーケンス番号の操作を抽象化するインターフェース(静的メソッドで実装)
 *
 * @tparam T シーケンス番号の型
 */
template <typename T>
class SequenceNumberOperationsInterface {
 public:
  virtual ~SequenceNumberOperationsInterface() = default;

  static T increment(T seq, T num = 1);
  static T decrement(T seq, T num = 1);

  static T wraparound(T seq);

  /**
   * @brief seq が seq_compared より新しいかどうかを判断する
   * @param seq シーケンス番号
   * @param seq_compared 比較するシーケンス番号
   * @return seq が seq_compared より新しい場合は true, そうでない場合は false
   */
  static bool isNewer(T seq, T seq_compared);

  /**
   * @brief seq が [start, end) の範囲にあるかどうかを判断する
   * @param seq シーケンス番号
   * @param start 範囲の開始
   * @param end 範囲の終了
   * @return seq が [start, end) の範囲にある場合は true, そうでない場合は false
   */
  static bool isInRange(T seq, T start, T end);

  /**
   * @brief 2つのシーケンス番号の差を計算する
   * @param seq1 シーケンス番号1 (小さい方)
   * @param seq2 シーケンス番号2 (大きい方)
   * @return シーケンス番号1とシーケンス番号2の差
   */
  static T difference(T seq_smaller, T seq_larger);
};

class Uint8SequenceNumberOperations
    : public SequenceNumberOperationsInterface<uint8_t> {
 public:
  using SequenceNumberType = uint8_t;
  constexpr static SequenceNumberType kHalfSize = 128;

  static SequenceNumberType increment(SequenceNumberType seq,
                                      SequenceNumberType num = 1) {
    return seq + num;
  }

  static SequenceNumberType decrement(SequenceNumberType seq,
                                      SequenceNumberType num = 1) {
    return seq - num;
  }

  static SequenceNumberType wraparound(SequenceNumberType seq) { return seq; }

  static bool isInRange(SequenceNumberType seq, SequenceNumberType start,
                        SequenceNumberType end) {
    if (end < start) {
      return seq >= start || seq < end;
    }
    return seq >= start && seq < end;
  }

  static bool isNewer(SequenceNumberType seq, SequenceNumberType seq_compared) {
    return isInRange(seq, seq_compared, seq_compared + kHalfSize);
  }

  static SequenceNumberType difference(SequenceNumberType seq1,
                                       SequenceNumberType seq2) {
    return seq1 - seq2;
  }
};

template <typename TimerIDType, typename DurationType>
class TimerInterface {
 public:
  virtual ~TimerInterface() = default;

  using TimerID = TimerIDType;
  using Duration = DurationType;

  virtual TimerID startTimer(Duration duration,
                             std::function<void()> callback) = 0;

  virtual void stopTimer(TimerID timer_id) = 0;

  virtual Duration getTimeout(TimerID timer_id) = 0;
  virtual Duration setTimeout(TimerID timer_id, Duration duration) = 0;

  virtual Duration getElapsedTime(TimerID timer_id) = 0;
};

template <typename SequenceNumberType, typename DurationType>
class TimeoutStrategyInterface {
 public:
  using SequenceNumber = SequenceNumberType;

  virtual ~TimeoutStrategyInterface() = default;

  using Duration = DurationType;

  virtual Duration getTimeout(SequenceNumber seq_num) = 0;
};

template <typename SequenceNumberType, typename DurationType,
          DurationType kDefaultTimeout = 1000>
class FixedTimeoutStrategy
    : public TimeoutStrategyInterface<SequenceNumberType, DurationType> {
 public:
  using SequenceNumber = SequenceNumberType;
  using Duration = DurationType;

  FixedTimeoutStrategy() {}

  Duration getTimeout(SequenceNumber seq_num) override {
    return kDefaultTimeout;
  }
};

template <typename SequenceNumberType>
class PacketTransmitterInterface {
 public:
  virtual ~PacketTransmitterInterface() = default;

  using SequenceNumber = SequenceNumberType;

  virtual void transmitData(SequenceNumber seq_num,
                            const std::vector<uint8_t>& data) = 0;

  virtual void transmitAck(SequenceNumber seq_num) = 0;

  virtual void transmitNack(SequenceNumber seq_num) = 0;
};

struct EmptyPacketAttributes {};

template <typename SequenceNumberType, typename PacketType>
class ReadOnlyPacketMapInterface {
 public:
  using SequenceNumber = SequenceNumberType;
  using Packet = PacketType;

  virtual ~ReadOnlyPacketMapInterface() = default;

  virtual std::shared_ptr<Packet> getPacket(SequenceNumber seq_num) = 0;

  virtual std::unordered_map<SequenceNumber, std::shared_ptr<Packet>>
  getPackets() = 0;
};

template <typename SequenceNumberType, typename PacketType>
class PacketMapInterface
    : public ReadOnlyPacketMapInterface<SequenceNumberType, PacketType> {
 public:
  using SequenceNumber = SequenceNumberType;
  using Packet = PacketType;

  virtual ~PacketMapInterface() = default;

  virtual std::unordered_map<SequenceNumber, std::shared_ptr<Packet>>&
  getPacketsRef() = 0;
};

template <typename SequenceNumberType, typename PacketType>
class PacketMap : public PacketMapInterface<SequenceNumberType, PacketType> {
 public:
  using SequenceNumber = SequenceNumberType;
  using Packet = PacketType;

  PacketMap() = default;

  std::unordered_map<SequenceNumber, std::shared_ptr<Packet>>& getPacketsRef()
      override {
    return packets_;
  }

  std::unordered_map<SequenceNumber, std::shared_ptr<Packet>> getPackets()
      override {
    return packets_;
  }

  std::shared_ptr<Packet> getPacket(SequenceNumber seq_num) override {
    if (packets_.find(seq_num) == packets_.end()) {
      return nullptr;
    }
    return packets_[seq_num];
  }

 private:
  std::unordered_map<SequenceNumber, std::shared_ptr<Packet>> packets_;
};

using DefaultMutexType = std::mutex;
using DefaultLockGuardType = std::lock_guard<DefaultMutexType>;

template <typename SequenceNumberType, typename SequenceNumberOperations,
          typename TimerIDType, typename DurationType,
          typename MutexType = DefaultMutexType,
          typename LockGuard = DefaultLockGuardType,
          typename PacketAttributesType = EmptyPacketAttributes,
          typename Logger = details::EmptyLogger>
class SRArqSender {
 public:
  virtual ~SRArqSender() {
    // Stop all active timers to prevent callbacks after destruction
    // This prevents use-after-free when onSendTimeout is called after
    // the SRArqSender instance is destroyed
    clearLifetimeGuard();
    try {
      LockGuard guard(send_buffer_mutex_);
      if (send_buffer_ && timer_) {
        auto& send_buffer = send_buffer_->getPacketsRef();
        for (auto& kv : send_buffer) {
          if (kv.second && kv.second->timer_id) {
            timer_->stopTimer(kv.second->timer_id);
            kv.second->timer_id = TimerID{};  // Clear timer_id
          }
        }
        // Clear the send buffer to prevent any further operations
        send_buffer.clear();
      }
      // Invalidate callbacks to prevent any callbacks during destruction
      callbacks_ = std::make_shared<EmptyCallbacks>();
    } catch (...) {
      // Ignore exceptions during destruction (best effort cleanup)
    }
  }

  using SequenceNumber = SequenceNumberType;
  using TimerID = TimerIDType;
  using Duration = DurationType;
  using Mutex = MutexType;
  using PacketAttributes = PacketAttributesType;

  enum class PacketStatus : uint8_t {
    kSent,
    kAcked,
    kNacked,
    kTimeout,
  };

  struct SendPacket {
    PacketStatus status;
    std::vector<uint8_t> data;
    TimerID timer_id;
    PacketAttributes attributes;

    SendPacket(PacketStatus status, const std::vector<uint8_t>& data,
               TimerID timer_id, PacketAttributes attributes)
        : status(status),
          data(data),
          timer_id(timer_id),
          attributes(attributes) {}
  };

  class CallbacksInterface {
   public:
    virtual ~CallbacksInterface() = default;

    virtual void onTimeout(SequenceNumber seq_num) = 0;
    virtual void onAck(SequenceNumber seq_num, bool is_acked) = 0;
    virtual void onWindowAvailable() = 0;
  };

  class EmptyCallbacks : public CallbacksInterface {
   public:
    void onTimeout(SequenceNumber seq_num) override {}
    void onAck(SequenceNumber seq_num, bool is_acked) override {}
    void onWindowAvailable() override {}
  };

  SRArqSender(
      std::shared_ptr<PacketMapInterface<SequenceNumber, SendPacket>>
          send_buffer,
      std::shared_ptr<TimerInterface<TimerID, Duration>> timer,
      std::shared_ptr<TimeoutStrategyInterface<SequenceNumber, Duration>>
          timeout_strategy,
      std::shared_ptr<PacketTransmitterInterface<SequenceNumber>>
          packet_transmitter,
      SequenceNumber sliding_window_length,
      std::shared_ptr<CallbacksInterface> callbacks, Mutex mutex)
      : send_buffer_(send_buffer),
        timer_(timer),
        timeout_strategy_(timeout_strategy),
        packet_transmitter_(packet_transmitter),
        sliding_window_start_(0),
        sliding_window_length_(sliding_window_length),
        callbacks_(callbacks),
        send_buffer_mutex_(mutex) {}

  /** Guard timer callbacks: skip if owner (e.g. SenderSession) was destroyed. */
  template <typename Owner>
  void setLifetimeGuard(const std::shared_ptr<Owner>& owner) {
    lifetime_guard_ = owner;
  }

  void clearLifetimeGuard() { lifetime_guard_.reset(); }

  /**
   * @brief データを送信する
   *
   * @param data 送信するデータ
   * @return std::pair<bool, SequenceNumber> 送信成功かどうかとシーケンス番号
   */
  std::pair<bool, SequenceNumber> send(const std::vector<uint8_t>& data) {
    // ロックを取得
    LockGuard guard(send_buffer_mutex_);
    auto result = searchAvailableSequenceNumber();
    if (!result.first) {
      // 空きがない - 内部状態を可視化
      auto& send_buffer = send_buffer_->getPacketsRef();
      size_t buffer_size = send_buffer.size();
      size_t pending_ack_count = 0;
      size_t timeout_count = 0;
      size_t nacked_count = 0;
      for (const auto& kv : send_buffer) {
        if (kv.second->status == PacketStatus::kSent) pending_ack_count++;
        if (kv.second->status == PacketStatus::kTimeout) timeout_count++;
        if (kv.second->status == PacketStatus::kNacked) nacked_count++;
      }
      Logger::log("send: no available sequence number | buffer_size=" +
                  std::to_string(buffer_size) +
                  " window_start=" + std::to_string(sliding_window_start_) +
                  " window_len=" + std::to_string(sliding_window_length_) +
                  " pending_ack=" + std::to_string(pending_ack_count) +
                  " timeout=" + std::to_string(timeout_count) +
                  " nacked=" + std::to_string(nacked_count));
      return std::make_pair(false, 0);
    }
    auto seq_num = result.second;
    Logger::log("send: send packet with seq_num: " + std::to_string(seq_num));
    send(seq_num, data);
    return std::make_pair(true, seq_num);
  }

  std::pair<bool, SequenceNumber> sendBySequenceNumber(
      SequenceNumber seq_num, const std::vector<uint8_t>& data) {
    // ロックを取得
    LockGuard guard(send_buffer_mutex_);
    // シーケンス番号がスライディングウィンドウを超えている->送信不可
    if (!SequenceNumberOperations::isNewer(
            seq_num, SequenceNumberOperations::increment(
                         sliding_window_start_, sliding_window_length_))) {
      Logger::log("sendBySequenceNumber: seq_num: " + std::to_string(seq_num) +
                  " is not in sliding window");
      return std::make_pair(false, 0);
    }
    // シーケンス番号がスライディングウィンドウより前->送信済みとみなす
    if (!SequenceNumberOperations::isInRange(
            seq_num, sliding_window_start_,
            SequenceNumberOperations::increment(sliding_window_start_,
                                                sliding_window_length_))) {
      Logger::log("sendBySequenceNumber: seq_num: " + std::to_string(seq_num) +
                  " is already sent");
      return std::make_pair(true, seq_num);
    }
    // シーケンス番号が送信バッファに存在する->送信済み
    if (isSequenceNumberHasData(seq_num)) {
      Logger::log("sendBySequenceNumber: seq_num: " + std::to_string(seq_num) +
                  " is already sent");
      return std::make_pair(true, seq_num);
    }
    send(seq_num, data);
    return std::make_pair(true, seq_num);
  }

  /**
   * @brief 受信したAckを処理する
   *
   * @param seq_num シーケンス番号
   * @param is_acked AckかNackか
   */
  void receiveAck(SequenceNumber seq_num, bool is_acked) {
    bool slided = false;
    {  // ロックを取得
      LockGuard guard(send_buffer_mutex_);

      auto& send_buffer = send_buffer_->getPacketsRef();

      if (send_buffer.find(seq_num) == send_buffer.end()) {
        Logger::log("receiveAck: seq_num=" + std::to_string(seq_num) +
                    " not in send_buffer (ignored, may be late or duplicate)");
        return;
      }

      timer_->stopTimer(send_buffer[seq_num]->timer_id);

      if (is_acked) {
        send_buffer[seq_num]->status = PacketStatus::kAcked;
        auto initial_sliding_window_start = sliding_window_start_;
        slideWindow();
        slided = initial_sliding_window_start != sliding_window_start_;
      } else {
        send_buffer[seq_num]->status = PacketStatus::kNacked;
        Logger::log("receiveAck: Nack seq_num=" + std::to_string(seq_num) +
                    " retransmitting");
        send(seq_num, send_buffer[seq_num]->data);
      }
    }  // ロックを解放

    Logger::log("receiveAck: seq_num=" + std::to_string(seq_num) +
                " is_acked=" + (is_acked ? "true" : "false") +
                (slided ? " window_slided" : ""));
    callbacks_->onAck(seq_num, is_acked);

    if (is_acked && slided) {
      // スライドウィンドウが空いたことを通知
      callbacks_->onWindowAvailable();
    }
  }

 private:
  /**
   * @brief パケットを送信する
   *
   * @param seq_num シーケンス番号
   * @param data 送信するデータ
   */
  void send(SequenceNumber seq_num, const std::vector<uint8_t>& data) {
    // 再送タイマーを設定（session GC 後の queued callback を weak guard で無効化）
    std::weak_ptr<void> guard = lifetime_guard_;
    auto timer_id = timer_->startTimer(
        timeout_strategy_->getTimeout(seq_num),
        [guard, this, seq_num]() {
          if (!guard.lock()) {
            return;
          }
          onSendTimeout(seq_num);
        });
    // 送信バッファに追加
    auto& send_buffer = send_buffer_->getPacketsRef();

    if (send_buffer.find(seq_num) != send_buffer.end()) {
      // パケットがすでに存在する場合, パケットを更新

      // タイマーが存在する場合, タイマーを停止
      if (send_buffer[seq_num]->timer_id) {
        timer_->stopTimer(send_buffer[seq_num]->timer_id);
      }

      send_buffer[seq_num]->status = PacketStatus::kSent;
      send_buffer[seq_num]->timer_id = timer_id;
      send_buffer[seq_num]->data = data;
      send_buffer[seq_num]->attributes = PacketAttributes();
    } else {
      // パケットが存在しない場合, 新しいパケットを作成
      send_buffer[seq_num] = std::make_shared<SendPacket>(
          PacketStatus::kSent, data, timer_id, PacketAttributes());
    }

    // パケットを送信
    packet_transmitter_->transmitData(seq_num, data);
  }

  bool isSequenceNumberHasData(SequenceNumber seq_num) {
    auto& send_buffer = send_buffer_->getPacketsRef();
    return send_buffer.find(seq_num) != send_buffer.end();
  }

  /**
   * @brief 空きのシーケンス番号を検索する
   *
   * @return std::pair<bool, SequenceNumber> 空きがあるかどうかとシーケンス番号
   */
  std::pair<bool, SequenceNumber> searchAvailableSequenceNumber() {
    auto& send_buffer = send_buffer_->getPacketsRef();
    for (SequenceNumber i = sliding_window_start_;
         SequenceNumberOperations::isInRange(
             i, sliding_window_start_,
             SequenceNumberOperations::increment(sliding_window_start_,
                                                 sliding_window_length_));
         i = SequenceNumberOperations::increment(i)) {
      if (!isSequenceNumberHasData(i)) {
        return std::make_pair(true, i);
      }
    }
    return std::make_pair(false, 0);
  }

  /**
   * @brief 再送タイムアウト時の処理
   *
   * @param seq_num シーケンス番号
   */
  void onSendTimeout(SequenceNumber seq_num) {
// Profiling: record timeout for root-cause analysis
#ifdef ARDUINO
    details::TimeoutProfiler::record_timeout();
#endif

    Logger::log("onSendTimeout: seq_num: " + std::to_string(seq_num));

    std::shared_ptr<SendPacket> send_packet;
    {  // ロックを取得
      LockGuard guard(send_buffer_mutex_);
      auto& send_buffer = send_buffer_->getPacketsRef();
      if (send_buffer.find(seq_num) == send_buffer.end()) {
        Logger::log("onSendTimeout: seq_num: " + std::to_string(seq_num) +
                    " is not in send buffer");
        // 無効なシーケンス番号のタイムアウト
        return;
      }
      send_packet = send_buffer[seq_num];
      send_packet->status = PacketStatus::kTimeout;
    }  // ロックを解放

    callbacks_->onTimeout(seq_num);

    {
      LockGuard guard(send_buffer_mutex_);
      send(seq_num, send_packet->data);
    }
  }

  /**
   * @brief スライドウィンドウを更新する
   */
  void slideWindow() {
    auto& send_buffer = send_buffer_->getPacketsRef();
    auto initial_sliding_window_start = sliding_window_start_;
    while (SequenceNumberOperations::isInRange(
        sliding_window_start_, initial_sliding_window_start,
        SequenceNumberOperations::increment(initial_sliding_window_start,
                                            sliding_window_length_))) {
      // Acked なら次に進める
      if (send_buffer.find(sliding_window_start_) != send_buffer.end() &&
          send_buffer[sliding_window_start_]->status == PacketStatus::kAcked) {
        sliding_window_start_ =
            SequenceNumberOperations::increment(sliding_window_start_, 1);
        continue;
      }
      // Ack でないパケットがある場合はスライドを停止
      break;
    }
    // スライディングウィンドウから外れたパケットを破棄
    for (SequenceNumber i = initial_sliding_window_start;
         SequenceNumberOperations::isInRange(i, initial_sliding_window_start,
                                             sliding_window_start_);
         i = SequenceNumberOperations::increment(i)) {
      // パケットを破棄
      send_buffer.erase(i);
    }
  }

  SequenceNumber sliding_window_start_;
  SequenceNumber sliding_window_length_;

  std::shared_ptr<TimerInterface<TimerID, Duration>> timer_;
  std::shared_ptr<TimeoutStrategyInterface<SequenceNumber, Duration>>
      timeout_strategy_;
  std::shared_ptr<PacketTransmitterInterface<SequenceNumber>>
      packet_transmitter_;
  std::shared_ptr<PacketMapInterface<SequenceNumber, SendPacket>> send_buffer_;

  std::shared_ptr<CallbacksInterface> callbacks_;

  std::weak_ptr<void> lifetime_guard_;
  Mutex send_buffer_mutex_;
};

/**
 * @brief 欠落したパケットを処理するインターフェース
 */
template <typename SequenceNumberType>
class MissingPacketHandlerInterface {
 public:
  virtual ~MissingPacketHandlerInterface() = default;

  using SequenceNumber = SequenceNumberType;

  virtual void onMissingPackets(
      const std::vector<SequenceNumber>& seq_nums) = 0;
};

/**
 * @brief 欠落したパケットを即時に通知するハンドラ
 */
template <typename SequenceNumberType>
class ImmediateMissingPacketHandler
    : public MissingPacketHandlerInterface<SequenceNumberType> {
 public:
  using SequenceNumber = SequenceNumberType;

  ImmediateMissingPacketHandler(
      std::shared_ptr<PacketTransmitterInterface<SequenceNumber>>
          packet_transmitter)
      : packet_transmitter_(packet_transmitter) {}

  void onMissingPacket(SequenceNumber seq_num) {
    packet_transmitter_->transmitNack(seq_num);
  }

  void onMissingPackets(const std::vector<SequenceNumber>& seq_nums) {
    for (const auto& seq_num : seq_nums) {
      onMissingPacket(seq_num);
    }
  }

 private:
  std::shared_ptr<PacketTransmitterInterface<SequenceNumber>>
      packet_transmitter_;
};

template <typename SequenceNumberType, typename SequenceNumberOperations,
          typename MutexType, typename LockGuard,
          typename Logger = details::EmptyLogger>
class SRArqReceiver {
 public:
  virtual ~SRArqReceiver() = default;

  using SequenceNumber = SequenceNumberType;
  using Mutex = MutexType;

  class CallbacksInterface {
   public:
    virtual ~CallbacksInterface() = default;

    virtual void onData(SequenceNumber seq_num) = 0;
    virtual void onDataOrdered(SequenceNumber seq_num,
                               std::vector<uint8_t> data) = 0;
  };

  class EmptyCallbacks : public CallbacksInterface {
   public:
    void onData(SequenceNumber seq_num) override {}
    void onDataOrdered(SequenceNumber seq_num,
                       std::vector<uint8_t> data) override {}
  };

  enum class ReceivePacketStatus : uint8_t {
    kReceived,
    kAcked,
    kNacked,
  };

  struct ReceivePacket {
    std::vector<uint8_t> data;
    ReceivePacketStatus status;

    ReceivePacket(const std::vector<uint8_t>& data, ReceivePacketStatus status)
        : data(data), status(status) {}
  };

  SRArqReceiver(
      std::shared_ptr<PacketMapInterface<SequenceNumber, ReceivePacket>>
          receive_buffer,
      std::shared_ptr<PacketTransmitterInterface<SequenceNumber>>
          packet_transmitter,
      SequenceNumber sliding_window_length,
      std::shared_ptr<CallbacksInterface> callbacks,
      std::shared_ptr<MissingPacketHandlerInterface<SequenceNumber>>
          missing_packet_handler,
      Mutex mutex)
      : receive_buffer_(receive_buffer),
        packet_transmitter_(packet_transmitter),
        sliding_window_start_(0),
        sliding_window_length_(sliding_window_length),
        missing_packet_handler_(missing_packet_handler),
        callbacks_(callbacks),
        receive_buffer_mutex_(mutex) {
    if (missing_packet_handler == nullptr) {
      missing_packet_handler_ =
          std::make_shared<ImmediateMissingPacketHandler<SequenceNumber>>(
              packet_transmitter);
    }
  }

  /**
   * @brief パケットを受信する
   *
   * @param seq_num シーケンス番号
   * @param data 受信したデータ
   */
  void receivePacket(SequenceNumber seq_num, const std::vector<uint8_t>& data) {
    {  // ロックを取得
      Logger::log("receivePacket: sliding_window_start: " +
                  std::to_string(sliding_window_start_) +
                  ", seq_num: " + std::to_string(seq_num));

      LockGuard guard(receive_buffer_mutex_);
      auto& receive_buffer = receive_buffer_->getPacketsRef();
      // sliding windowの範囲外のパケットは, Ack済みと判断して破棄
      if (!SequenceNumberOperations::isInRange(
              seq_num, sliding_window_start_,
              SequenceNumberOperations::increment(sliding_window_start_,
                                                  sliding_window_length_))) {
        Logger::log("receivePacket: seq_num: " + std::to_string(seq_num) +
                    " is out of range, resend ack");
        // Ack再送
        packet_transmitter_->transmitAck(seq_num);
        return;
      }

      // パケットがすでに受信済み
      if (receive_buffer.find(seq_num) != receive_buffer.end()) {
        // Acked -> Ack再送
        if (receive_buffer[seq_num]->status == ReceivePacketStatus::kAcked) {
          Logger::log("receivePacket: seq_num: " + std::to_string(seq_num) +
                      " is already acked, resend ack");
          packet_transmitter_->transmitAck(seq_num);
          return;
        }
        // Nacked or Received -> 過去に受信したが, 再送されたパケット ->
        // データを更新して再び通知する
      }

      // データを更新
      receive_buffer[seq_num] =
          std::make_shared<ReceivePacket>(data, ReceivePacketStatus::kReceived);
      Logger::log("receivePacket: new Data seq_num=" + std::to_string(seq_num) +
                  " data_size=" + std::to_string(data.size()));
    }  // ロックを解放
    // アプリケーションに通知
    callbacks_->onData(seq_num);
  }

  /**
   * @brief Ackを送信する
   *
   * @param seq_num シーケンス番号
   * @param is_acked AckかNackか
   */
  void sendAck(SequenceNumber seq_num, bool is_acked) {
    std::vector<SequenceNumber> out_of_order_packets;

    {  // ロックを取得
      LockGuard guard(receive_buffer_mutex_);
      auto& receive_buffer = receive_buffer_->getPacketsRef();
      if (receive_buffer.find(seq_num) == receive_buffer.end()) {
        // 無効なシーケンス番号のAck
        return;
      }

      if (is_acked) {
        receive_buffer[seq_num]->status = ReceivePacketStatus::kAcked;
        slideWindow();
        packet_transmitter_->transmitAck(seq_num);
      } else {
        receive_buffer[seq_num]->status = ReceivePacketStatus::kNacked;
        packet_transmitter_->transmitNack(seq_num);
      }

      // seq_numがスライドウィンドウの範囲内か?
      // そうでないなら, MissingPacketHandlerに通知しない
      if (!SequenceNumberOperations::isInRange(
              seq_num, sliding_window_start_,
              SequenceNumberOperations::increment(sliding_window_start_,
                                                  sliding_window_length_))) {
        return;
      }

      // スライドウィンドウの順序チェック
      for (SequenceNumber i = sliding_window_start_;
           SequenceNumberOperations::isInRange(i, sliding_window_start_,
                                               seq_num);
           i = SequenceNumberOperations::increment(i)) {
        if (receive_buffer.find(i) == receive_buffer.end()) {
          out_of_order_packets.push_back(i);
        }
      }
    }  // ロックを解放

    Logger::log(
        "sendAck: seq_num=" + std::to_string(seq_num) +
        " is_acked=" + std::to_string(is_acked) +
        " sliding_window_start=" + std::to_string(sliding_window_start_) +
        " out_of_order_count=" + std::to_string(out_of_order_packets.size()));
    if (!out_of_order_packets.empty()) {
      std::string missing_list;
      for (size_t i = 0; i < out_of_order_packets.size(); ++i) {
        if (i > 0) missing_list += ",";
        missing_list += std::to_string(out_of_order_packets[i]);
      }
      Logger::log("sendAck: requesting Nack for missing seq_nums: " +
                  missing_list);
    }
    missing_packet_handler_->onMissingPackets(out_of_order_packets);
  }

  std::pair<bool, std::vector<uint8_t>> getReceivedData(
      SequenceNumber seq_num) {
    {  // ロックを取得
      LockGuard guard(receive_buffer_mutex_);
      auto& receive_buffer = receive_buffer_->getPacketsRef();
      if (receive_buffer.find(seq_num) == receive_buffer.end()) {
        return std::make_pair(false, std::vector<uint8_t>());
      }
      return std::make_pair(true, receive_buffer[seq_num]->data);
    }  // ロックを解放
  }

 private:
  void slideWindow() {
    auto& receive_buffer = receive_buffer_->getPacketsRef();
    auto initial_sliding_window_start = sliding_window_start_;
    while (SequenceNumberOperations::isInRange(
        sliding_window_start_, initial_sliding_window_start,
        SequenceNumberOperations::increment(initial_sliding_window_start,
                                            sliding_window_length_))) {
      // Acked なら次に進める
      if (receive_buffer.find(sliding_window_start_) != receive_buffer.end() &&
          receive_buffer[sliding_window_start_]->status ==
              ReceivePacketStatus::kAcked) {
        sliding_window_start_ =
            SequenceNumberOperations::increment(sliding_window_start_, 1);
        continue;
      }
      // Ack でないパケットがある場合はスライドを停止
      break;
    }
    // スライディングウィンドウから外れたパケットを破棄
    for (SequenceNumber i = initial_sliding_window_start;
         SequenceNumberOperations::isInRange(i, initial_sliding_window_start,
                                             sliding_window_start_);
         i = SequenceNumberOperations::increment(i)) {
      if (receive_buffer.find(i) != receive_buffer.end()) {
        // 整列データを通知
        callbacks_->onDataOrdered(i, receive_buffer[i]->data);
        // パケットを破棄
        receive_buffer.erase(i);
      }
    }
  }

  std::shared_ptr<PacketTransmitterInterface<SequenceNumber>>
      packet_transmitter_;
  SequenceNumber sliding_window_start_;
  SequenceNumber sliding_window_length_;
  std::shared_ptr<CallbacksInterface> callbacks_;
  std::shared_ptr<MissingPacketHandlerInterface<SequenceNumber>>
      missing_packet_handler_;
  std::shared_ptr<PacketMapInterface<SequenceNumber, ReceivePacket>>
      receive_buffer_;

  Mutex receive_buffer_mutex_;
};

}  // namespace srarq

#endif
