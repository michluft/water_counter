import * as exposes from 'zigbee-herdsman-converters/lib/exposes';
import * as reporting from 'zigbee-herdsman-converters/lib/reporting';

const e = exposes.presets;
const ea = exposes.access;

const FLOW_ENDPOINT = 10;
const TOTAL_ENDPOINT = 11;
const INTERVAL_ENDPOINT = 12;

export default {
    zigbeeModel: ['Zehner Water Meter'],
    model: 'Zehner Water Meter',
    vendor: 'Michael',
    description: 'ESP32-H2 Zigbee Router mit Wasserzähler (Durchfluss + Gesamtverbrauch, beide als Float)',

    endpoint: (device) => {
        return {flow: FLOW_ENDPOINT, water_total: TOTAL_ENDPOINT, report_interval: INTERVAL_ENDPOINT};
    },

    fromZigbee: [
        {
            cluster: 'genAnalogInput',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                if (msg.data.presentValue === undefined) return;

                if (msg.endpoint.ID === FLOW_ENDPOINT) {
                    return {flow: Math.round(msg.data.presentValue * 100) / 100};
                }
                if (msg.endpoint.ID === TOTAL_ENDPOINT) {
                    return {water_total: Math.round(msg.data.presentValue)};
                }
                return;
            },
        },
        {
            cluster: 'genAnalogOutput',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                if (msg.data.presentValue === undefined) return;
                if (msg.endpoint.ID === INTERVAL_ENDPOINT) {
                    return {report_interval: Math.round(msg.data.presentValue)};
                }
                return;
            },
        },
    ],

    toZigbee: [
        {
            key: ['report_interval'],
            convertSet: async (entity, key, value, meta) => {
                const seconds = Math.max(5, Math.min(3600, Math.round(Number(value))));
                const ep = meta.device.getEndpoint(INTERVAL_ENDPOINT) || entity;
                await ep.write('genAnalogOutput', {presentValue: seconds});
                return {state: {report_interval: seconds}};
            },
            convertGet: async (entity, key, meta) => {
                const ep = meta.device.getEndpoint(INTERVAL_ENDPOINT) || entity;
                await ep.read('genAnalogOutput', ['presentValue']);
            },
        },
    ],

    exposes: [
        e.numeric('flow', ea.STATE)
            .withUnit('L/min')
            .withDescription('Momentaner Wasserdurchfluss'),
        e.numeric('water_total', ea.STATE)
            .withUnit('L')
            .withDescription('Aufsummierter Gesamtverbrauch'),
        e.numeric('report_interval', ea.ALL)
            .withUnit('s')
            .withValueMin(5)
            .withValueMax(3600)
            .withEndpoint('report_interval')
            .withDescription('Zigbee-Reportintervall (mindestens 5 s)'),
    ],

    // Firmware-Update über Zigbee, siehe README.
    ota: true,

    configure: async (device, coordinatorEndpoint, logger) => {
        const flowEp = device.getEndpoint(FLOW_ENDPOINT);
        await reporting.bind(flowEp, coordinatorEndpoint, ['genAnalogInput']);

        const totalEp = device.getEndpoint(TOTAL_ENDPOINT);
        await reporting.bind(totalEp, coordinatorEndpoint, ['genAnalogInput']);

        const intervalEp = device.getEndpoint(INTERVAL_ENDPOINT);
        await intervalEp.read('genAnalogOutput', ['presentValue']);
    },
};
