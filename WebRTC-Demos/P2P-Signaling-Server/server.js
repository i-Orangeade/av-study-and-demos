'use strict';

const { WebSocketServer } = require('ws');

const port = Number.parseInt(process.env.PORT || '8080', 10);
const wss = new WebSocketServer({ port });

const rooms = new Map();

function send(ws, message) {
  if (ws.readyState === ws.OPEN) {
    ws.send(JSON.stringify(message));
  }
}

function getRoom(roomId) {
  let room = rooms.get(roomId);
  if (!room) {
    room = new Map();
    rooms.set(roomId, room);
  }
  return room;
}

function broadcast(roomId, message, exceptUserId) {
  const room = rooms.get(roomId);
  if (!room) {
    return;
  }

  for (const [userId, client] of room) {
    if (userId !== exceptUserId) {
      send(client.ws, message);
    }
  }
}

function cleanupClient(client) {
  if (!client.roomId || !client.userId) {
    return;
  }

  const room = rooms.get(client.roomId);
  if (!room) {
    return;
  }

  const current = room.get(client.userId);
  if (current && current.ws === client.ws) {
    room.delete(client.userId);
    broadcast(client.roomId, {
      type: 'peer-left',
      roomId: client.roomId,
      userId: client.userId
    });
  }

  if (room.size === 0) {
    rooms.delete(client.roomId);
  }
}

function handleJoin(client, message) {
  const { roomId, userId } = message;
  if (!roomId || !userId) {
    send(client.ws, { type: 'error', reason: 'join requires roomId and userId' });
    return;
  }

  cleanupClient(client);

  const room = getRoom(roomId);
  if (room.has(userId)) {
    send(client.ws, { type: 'error', reason: `userId ${userId} already exists in room ${roomId}` });
    return;
  }

  client.roomId = roomId;
  client.userId = userId;
  room.set(userId, client);

  const peers = [...room.keys()].filter((peerId) => peerId !== userId);
  send(client.ws, { type: 'joined', roomId, userId, peers });
  broadcast(roomId, { type: 'peer-joined', roomId, userId }, userId);
}

function forwardToPeer(client, message) {
  const { type, roomId, to } = message;
  if (!client.roomId || !client.userId) {
    send(client.ws, { type: 'error', reason: `${type} requires join first` });
    return;
  }

  if (roomId !== client.roomId) {
    send(client.ws, { type: 'error', reason: `${type} roomId mismatch` });
    return;
  }

  const room = rooms.get(roomId);
  const peer = room && room.get(to);
  if (!peer) {
    send(client.ws, { type: 'error', reason: `peer ${to} not found in room ${roomId}` });
    return;
  }

  send(peer.ws, {
    ...message,
    from: client.userId
  });
}

function handleMessage(client, raw) {
  let message;
  try {
    message = JSON.parse(raw);
  } catch (error) {
    send(client.ws, { type: 'error', reason: 'message must be valid JSON' });
    return;
  }

  switch (message.type) {
    case 'join':
      handleJoin(client, message);
      break;
    case 'leave':
      cleanupClient(client);
      send(client.ws, { type: 'left' });
      break;
    case 'offer':
    case 'answer':
    case 'candidate':
      forwardToPeer(client, message);
      break;
    default:
      send(client.ws, { type: 'error', reason: `unknown message type: ${message.type}` });
      break;
  }
}

wss.on('connection', (ws) => {
  const client = {
    ws,
    roomId: null,
    userId: null
  };

  send(ws, { type: 'welcome', message: 'WebRTC signaling server connected' });

  ws.on('message', (raw) => handleMessage(client, raw.toString()));
  ws.on('close', () => cleanupClient(client));
  ws.on('error', () => cleanupClient(client));
});

console.log(`WebRTC signaling server listening on ws://0.0.0.0:${port}`);
