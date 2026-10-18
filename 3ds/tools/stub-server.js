// Runs a local PictoCard server (the checkout of the PictoCard website given
// as the first argument) with nothing leaving the machine, for testing the 3DS app:
//   - email goes to nodemailer's JSON transport and is logged
//   - verification codes are always 123456
//   - Wii delivery is switched on and "sent" letters are logged, not delivered
//
//   PORT=3011 node tools/stub-server.js /path/to/PictoCard
'use strict';

const path = require('path');
const site = path.resolve(process.argv[2] || '.');
const load = (m) => require(path.join(site, m));

const crypto = require('crypto');
const randomInt = crypto.randomInt;
crypto.randomInt = (min, max, ...rest) =>
  min === 100000 && max === 1000000 ? 123456 : randomInt(min, max, ...rest);

const nodemailer = load('node_modules/nodemailer');
const createTransport = nodemailer.createTransport;
nodemailer.createTransport = () => {
  const t = createTransport({ jsonTransport: true });
  const send = t.sendMail.bind(t);
  t.sendMail = async (mail) => {
    console.log(`[stub mail] to=${mail.to} subject=${JSON.stringify(mail.subject)}`);
    return send(mail);
  };
  return t;
};

const wc24 = load('lib/wc24');
wc24.wc24Config = () => ({ sender: '3312950551375680' });
wc24.sendWiiMail = async (mail) => {
  console.log(`[stub wii] to=${mail.recipient} from=${JSON.stringify(mail.senderName)} text=${JSON.stringify(mail.text)}` +
    ` jpeg=${mail.jpeg ? mail.jpeg.length : 0} letterhead=${mail.letterheadArc ? mail.letterheadArc.length : 0}`);
};
wc24.requestFriend = async (code) => {
  console.log(`[stub wii] friend request to ${code}`);
  return 'sent';
};

process.chdir(site);
load('server.js');
