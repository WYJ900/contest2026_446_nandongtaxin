const UUID = {
  service: '0000fff0-0000-1000-8000-00805f9b34fb',
  tx: '0000fff1-0000-1000-8000-00805f9b34fb',
  rx: '0000fff2-0000-1000-8000-00805f9b34fb',
};
const events = { ping: 'pong', time_sync: 'time_sync', phone_data: 'phone_data',
  ride_status: 'ride_status', ride_latest: 'ride_latest', ride_review: 'ride_review' };

export class RideBleTransport {
  constructor({ onMessage = () => {}, onDisconnect = () => {}, timeoutMs = 10000 } = {}) {
    Object.assign(this, { onMessage, onDisconnect, timeoutMs });
    this.queue = Promise.resolve();
    this.session = 0;
    this.pending = null;
    this.connected = false;
    this.receive = (event) => this.accept(event.target.value);
    this.disconnected = () => this.reset();
  }

  async connect(device) {
    this.disconnect();
    this.device = device;
    this.decoder = new TextDecoder();
    this.buffer = '';
    device.addEventListener('gattserverdisconnected', this.disconnected);
    try {
      const server = await device.gatt.connect();
      const service = await server.getPrimaryService(UUID.service);
      this.tx = await service.getCharacteristic(UUID.tx);
      this.rx = await service.getCharacteristic(UUID.rx);
      this.tx.addEventListener('characteristicvaluechanged', this.receive);
      await this.tx.startNotifications();
      this.connected = true;
    } catch (error) {
      this.disconnect();
      throw error;
    }
  }

  reset() {
    this.session++;
    const wasConnected = this.connected;
    this.connected = false;
    this.pending?.reject(new Error('手表连接已断开'));
    this.pending = null;
    this.tx?.removeEventListener('characteristicvaluechanged', this.receive);
    this.device?.removeEventListener('gattserverdisconnected', this.disconnected);
    this.tx = null;
    this.rx = null;
    this.buffer = '';
    if (wasConnected) this.onDisconnect();
  }

  disconnect() {
    const device = this.device;
    this.reset();
    this.device = null;
    if (device?.gatt.connected) device.gatt.disconnect();
  }

  accept(bytes) {
    this.buffer += this.decoder.decode(bytes, { stream: true });
    if (this.buffer.length > 4096) {
      this.pending?.reject(new Error('手表回复过长'));
      this.disconnect();
      return;
    }
    let end;
    while ((end = this.buffer.indexOf('\n')) !== -1) {
      const line = this.buffer.slice(0, end).trim();
      this.buffer = this.buffer.slice(end + 1);
      if (!line) continue;
      let message;
      try {
        message = JSON.parse(line);
        if (!message || typeof message !== 'object' || Array.isArray(message)) throw new Error();
      } catch {
        this.pending?.reject(new Error('手表回复格式错误'));
        this.disconnect();
        return;
      }
      if (message.ok === false) this.pending?.reject(new Error(message.error || '设备操作失败'));
      else if (this.pending && message.event === this.pending.event) this.pending.resolve(message);
      this.onMessage(message);
    }
  }

  request(payload) {
    const session = this.session;
    const task = this.queue.then(() => {
      if (!this.connected || session !== this.session) throw new Error('手表未连接');
      return this.send(payload);
    });
    this.queue = task.catch(() => {});
    return task;
  }

  async send(payload) {
    if (!events[payload.cmd]) throw new Error('不支持的手表命令');
    const frame = new TextEncoder().encode(`${JSON.stringify(payload)}\n`);
    if (frame.length > 767) throw new Error('设备命令过长');
    let timer;
    const reply = new Promise((resolve, reject) => {
      this.pending = { event: events[payload.cmd], resolve, reject };
    });
    const deadline = new Promise((resolve, reject) => {
      timer = setTimeout(() => {
        reject(new Error('手表回复超时，请重新连接'));
        this.disconnect();
      }, this.timeoutMs);
    });
    // A disconnect can reject the reply while an ATT write is still pending.
    reply.catch(() => {});
    try {
      const exchange = async () => {
        for (let offset = 0; offset < frame.length; offset += 18) {
          if (!this.rx) throw new Error('手表连接已断开');
          const chunk = frame.slice(offset, offset + 18);
          if (typeof this.rx.writeValueWithResponse === 'function') {
            await this.rx.writeValueWithResponse(chunk);
          } else {
            await this.rx.writeValue(chunk);
          }
        }
        return await reply;
      };
      return await Promise.race([exchange(), deadline]);
    } finally {
      clearTimeout(timer);
      this.pending = null;
    }
  }
}
