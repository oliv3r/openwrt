// SPDX-License-Identifier: GPL-2.0-only

#include <net/dsa.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <net/flow_offload.h>

#include <asm/mach-rtl838x/mach-rtl83xx.h>
#include "rtl83xx.h"
#include "rtl838x.h"

/*
 * Parse the flow rule for the matching conditions
 */
static int rtl83xx_parse_flow_rule(struct rtl838x_switch_priv *priv,
			      struct flow_rule *rule, struct rtl83xx_flow *flow)
{
	struct flow_dissector *dissector = rule->match.dissector;

	pr_info("In %s\n", __func__);
	/* KEY_CONTROL and KEY_BASIC are needed for forming a meaningful key */
	if ((dissector->used_keys & BIT(FLOW_DISSECTOR_KEY_CONTROL)) == 0 ||
	    (dissector->used_keys & BIT(FLOW_DISSECTOR_KEY_BASIC)) == 0) {
		pr_info("Cannot form TC key: used_keys = 0x%x\n", dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		pr_info("%s: BASIC\n", __func__);
		flow_rule_match_basic(rule, &match);
		if (match.key->n_proto == htons(ETH_P_ARP))
			flow->rule.frame_type = 0;
		if (match.key->n_proto == htons(ETH_P_IP))
			flow->rule.frame_type = 2;
		if (match.key->n_proto == htons(ETH_P_IPV6))
			flow->rule.frame_type = 3;
		if ((match.key->n_proto == htons(ETH_P_ARP)) || flow->rule.frame_type)
			flow->rule.frame_type_m = 3;
		if (flow->rule.frame_type >= 2) {
			if (match.key->ip_proto == IPPROTO_UDP)
				flow->rule.frame_type_l4 = 0;
			if (match.key->ip_proto == IPPROTO_TCP)
				flow->rule.frame_type_l4 = 1;
			if (match.key->ip_proto == IPPROTO_ICMP
				|| match.key->ip_proto ==IPPROTO_ICMPV6)
				flow->rule.frame_type_l4 = 2;
			if (match.key->ip_proto == IPPROTO_TCP)
				flow->rule.frame_type_l4 = 3;
			if ((match.key->ip_proto == IPPROTO_UDP) || flow->rule.frame_type_l4)
				flow->rule.frame_type_l4_m = 7;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		pr_info("%s: ETH_ADDR\n", __func__);
		flow_rule_match_eth_addrs(rule, &match);
		ether_addr_copy(flow->rule.dmac, match.key->dst);
		ether_addr_copy(flow->rule.dmac_m, match.mask->dst);
		ether_addr_copy(flow->rule.smac, match.key->src);
		ether_addr_copy(flow->rule.smac_m, match.mask->src);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		pr_info("%s: VLAN\n", __func__);
		flow_rule_match_vlan(rule, &match);
		flow->rule.itag = match.key->vlan_id;
		flow->rule.itag_m = match.mask->vlan_id;
		// TODO: What about match.key->vlan_priority ?
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match;

		pr_info("%s: IPV4\n", __func__);
		flow_rule_match_ipv4_addrs(rule, &match);
		// TODO: Is a flag needed because the mask might be 0?
		flow->rule.dip = match.key->dst;
		flow->rule.dip_m = match.mask->dst;
		flow->rule.sip = match.key->src;
		flow->rule.sip_m = match.mask->src;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		pr_info("%s: PORTS\n", __func__);
		flow_rule_match_ports(rule, &match);
		flow->rule.dport = match.key->dst;
		flow->rule.dport_m = match.mask->dst;
		flow->rule.sport = match.key->src;
		flow->rule.sport_m = match.mask->src;
	}

	// TODO: IPv6, ICMP
	return 0;
}

static int rtl83xx_parse_fwd(const struct flow_action_entry *act, struct rtl83xx_flow *flow)
{
	struct net_device *dev = act->dev;

	if(!netdev_uses_dsa(dev)) {
		netdev_info(dev, "%s: not a DSA device.\n", __func__);
		return -EINVAL;
	}

	flow->rule.fwd_data = dev->dsa_ptr->index;
	// Forwading action qualifiers: FORCE, Skip strom and STP filters
	flow->rule.fwd_data |= BIT(12) | BIT(11) | BIT(10);

	pr_info("%s: data: %04x\n", __func__, flow->rule.fwd_data);
	return 0;
}

static int rtl83xx_add_flow(struct rtl838x_switch_priv *priv, struct flow_cls_offload *f,
			    struct rtl83xx_flow *flow)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	const struct flow_action_entry *act;
	int i, err;

	pr_info("%s\n", __func__);

	rtl83xx_parse_flow_rule(priv, rule, flow);
	
	flow_action_for_each(i, act, &rule->action) {
		switch (act->id) {
		case FLOW_ACTION_DROP:
			pr_info("%s: DROP\n", __func__);
			flow->rule.drop = 1;
			return 0;
		case FLOW_ACTION_TRAP:
			pr_info("%s: TRAP\n", __func__);
			flow->rule.fwd_data = priv->cpu_port;
			// Forwading action:  REDIRECT TO PORT, FORCE, Skip filters
			flow->rule.fwd_data |= 0x4 << 13 | BIT(12) | BIT(11) | BIT(10);
			break;
		case FLOW_ACTION_MANGLE:
		case FLOW_ACTION_ADD:
			pr_info("%s: MANGLE/ADD\n", __func__);
			break;
		case FLOW_ACTION_CSUM:
			pr_info("%s: CSUM\n", __func__);
			break;
		case FLOW_ACTION_REDIRECT:
			pr_info("%s: REDIRECT\n", __func__);
			err = rtl83xx_parse_fwd(act, flow);
			if (err)
				return err;
			flow->rule.fwd_data |= 0x4 << 13;  // action: redirect to port-id
			break;
		case FLOW_ACTION_MIRRED:
			pr_info("%s: MIRRED\n", __func__);
			err = rtl83xx_parse_fwd(act, flow);
			if (err)
				return err;
			flow->rule.fwd_data |= 0x2 << 13;  // action: copy to port-id
			break;
		default:
			pr_info("%s: Flow action not supported: %d\n", __func__, act->id);
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static const struct rhashtable_params tc_ht_params = {
	.head_offset = offsetof(struct rtl83xx_flow, node),
	.key_offset = offsetof(struct rtl83xx_flow, cookie),
	.key_len = sizeof(((struct rtl83xx_flow *)0)->cookie),
	.automatic_shrinking = true,
};

static int rtl83xx_configure_flower(struct rtl838x_switch_priv *priv,
				    struct flow_cls_offload *f)
{
	struct rtl83xx_flow *flow;
	int err = 0;

	pr_info("In %s\n", __func__);

	rcu_read_lock();
	pr_info("Cookie %08lx\n", f->cookie);
	flow = rhashtable_lookup(&priv->tc_ht, &f->cookie, tc_ht_params);
	if (flow) {
		pr_info("%s: Got flow\n", __func__);
		err = -EEXIST;
		goto rcu_unlock;
	}

rcu_unlock:
	rcu_read_unlock();
	if (flow)
		goto out;
	pr_info("%s: New flow\n", __func__);

	flow = kzalloc(sizeof(*flow), GFP_KERNEL);
	if (!flow) {
		err = -ENOMEM;
		goto out;
	}

	flow->cookie = f->cookie;
	flow->priv = priv;

	err = rhashtable_insert_fast(&priv->tc_ht, &flow->node, tc_ht_params);
	if (err) {
		pr_err("Could not insert add new rule\n");
		goto out_free;
	}

	// TODO: Here, we will need to identify whether we already know this flow

	rtl83xx_add_flow(priv, f, flow); // TODO: check error

	return priv->r->pie_rule_add(priv, &flow->rule);
out_free:
	kfree(flow);
out:
	return err;
}

static int rtl83xx_delete_flower(struct rtl838x_switch_priv *priv,
				 struct flow_cls_offload * cls_flower)
{
	struct rtl83xx_flow *flow;

	pr_info("In %s\n", __func__);
	flow = rhashtable_lookup_fast(&priv->tc_ht, &cls_flower->cookie, tc_ht_params);
	if (!flow)
		return -EINVAL;

	priv->r->pie_rule_rm(priv, &flow->rule);

	// TODO: kfree
	// TODO: Unlink entry from hash-table

	return 0;
}

static int rtl83xx_stats_flower(struct rtl838x_switch_priv *priv,
				struct flow_cls_offload * cls_flower)
{
	struct rtl83xx_flow *flow;
	unsigned long lastused = 0;

	pr_info("In %s\n", __func__);
	
	flow = rhashtable_lookup_fast(&priv->tc_ht, &cls_flower->cookie, tc_ht_params);
	if (!flow)
		return -1;

	flow_stats_update(&cls_flower->stats, 100, 10, lastused);
	return 0;
}

static int rtl83xx_setup_tc_cls_flower(struct rtl838x_switch_priv *priv,
				       struct flow_cls_offload *cls_flower)
{
	pr_info("%s: %d\n", __func__, cls_flower->command);
	switch (cls_flower->command) {
	case FLOW_CLS_REPLACE:
		return rtl83xx_configure_flower(priv, cls_flower);
	case FLOW_CLS_DESTROY:
		return rtl83xx_delete_flower(priv, cls_flower);
	case FLOW_CLS_STATS:
		return rtl83xx_stats_flower(priv, cls_flower);
	default:
		return -EOPNOTSUPP;
	}
}


static int rtl83xx_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
				     void *cb_priv)
{
	struct rtl838x_switch_priv *priv = cb_priv;

	pr_info("%s: %d\n", __func__, type);
	switch (type) {
	case TC_SETUP_CLSFLOWER:
		pr_info("%s: TC_SETUP_CLSFLOWER\n", __func__);
		return rtl83xx_setup_tc_cls_flower(priv, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(rtl83xx_block_cb_list);

int rtl83xx_setup_tc(struct net_device *dev, enum tc_setup_type type, void *type_data)
{
	struct rtl838x_switch_priv *priv;
	struct flow_block_offload *f = type_data;
	static bool first_time = true;
	int err;

	pr_info("%s: %d\n", __func__, type);

	if(!netdev_uses_dsa(dev)) {
		pr_info("%s: no DSA\n", __func__);
		return 0;
	}
	priv = dev->dsa_ptr->ds->priv;

	switch (type) {
	case TC_SETUP_BLOCK:
		pr_info("%s: setting up CB\n", __func__);

		if (first_time) {
			pr_info("Initializing rhash\n");
			first_time = false;
			err = rhashtable_init(&priv->tc_ht, &tc_ht_params);
			if (err)
				pr_info("THAT DID NOT GO WELL\n");
		}

		f->unlocked_driver_cb = true;
		return flow_block_cb_setup_simple(type_data,
						  &rtl83xx_block_cb_list,
						  rtl83xx_setup_tc_block_cb,
						  priv, priv, true);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
