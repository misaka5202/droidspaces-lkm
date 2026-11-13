// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * Based on Rusty Russell's IPv4 NAT code. Development of IPv6 NAT
 * funded by Astaro.
 */

#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>

#include <net/netfilter/nf_nat.h>

struct ip6table_nat_pernet {
	struct nf_hook_ops *nf_nat_ops;
};

static unsigned int ip6table_nat_net_id __read_mostly;

static const struct xt_table nf_nat_ipv6_table = {
	.name		= "nat",
	.valid_hooks	= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_LOCAL_IN),
	.me		= THIS_MODULE,
	.af		= NFPROTO_IPV6,
};

/*
 * ip6t_do_table 的签名在 6.1 变了:
 *
 *   5.15 及更早: unsigned int ip6t_do_table(struct sk_buff *skb,
 *                                          const struct nf_hook_state *state,
 *                                          struct xt_table *table)
 *   6.1 及更新:  unsigned int ip6t_do_table(void *priv, struct sk_buff *skb,
 *                                          const struct nf_hook_state *state)
 *
 * 而 nf_hookfn 本身在 5.15 就已经是 (void *priv, sk_buff*, state*) 了。
 * 直接把它当 .hook 赋进去, 老内核上是把 3 参数函数指针塞进 3 参数的槽,
 * 类型对不上 (老内核的第三个参数是 xt_table*, 不是 nf_hook_state*)。
 *
 * 这里用一个薄 wrapper 把两边接起来: 新版直接转发; 老版从 priv 取回
 * xt_table, 再按老签名调用。
 */
static unsigned int __nocfi droid_nat_hook_entry(void *priv,
                                                 struct sk_buff *skb,
                                                 const struct nf_hook_state *state)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
    return droid_nat_ip6t_do_table(priv, skb, state);
#else
    return droid_nat_ip6t_do_table_legacy(priv, skb, state);
#endif
}

static const struct nf_hook_ops nf_nat_ipv6_ops[] = {
	{
		.hook		= droid_nat_hook_entry,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	{
		.hook		= droid_nat_hook_entry,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
	{
		.hook		= droid_nat_hook_entry,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	{
		.hook		= droid_nat_hook_entry,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
};

static int ip6t_nat_register_lookups(struct net *net)
{
	struct ip6table_nat_pernet *xt_nat_net;
	struct nf_hook_ops *ops;
	struct xt_table *table;
	int i, ret;

	table = xt_find_table(net, NFPROTO_IPV6, "nat");
	if (WARN_ON_ONCE(!table))
		return -ENOENT;

	xt_nat_net = net_generic(net, ip6table_nat_net_id);
	ops = kmemdup(nf_nat_ipv6_ops, sizeof(nf_nat_ipv6_ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(nf_nat_ipv6_ops); i++) {
		ops[i].priv = table;
		ret = nf_nat_ipv6_register_fn(net, &ops[i]);
		if (ret) {
			while (i)
				nf_nat_ipv6_unregister_fn(net, &ops[--i]);

			kfree(ops);
			return ret;
		}
	}

	xt_nat_net->nf_nat_ops = ops;
	return 0;
}

static void ip6t_nat_unregister_lookups(struct net *net)
{
	struct ip6table_nat_pernet *xt_nat_net = net_generic(net, ip6table_nat_net_id);
	struct nf_hook_ops *ops = xt_nat_net->nf_nat_ops;
	int i;

	if (!ops)
		return;

	for (i = 0; i < ARRAY_SIZE(nf_nat_ipv6_ops); i++)
		nf_nat_ipv6_unregister_fn(net, &ops[i]);

	kfree(ops);
}

static int ip6table_nat_table_init(struct net *net)
{
	struct ip6t_replace *repl;
	int ret;

	repl = ip6t_alloc_initial_table(&nf_nat_ipv6_table);
	if (repl == NULL)
		return -ENOMEM;
	ret = ip6t_register_table(net, &nf_nat_ipv6_table, repl,
				  NULL);
	if (ret < 0) {
		kfree(repl);
		return ret;
	}

	ret = ip6t_nat_register_lookups(net);
	if (ret < 0)
		ip6t_unregister_table_exit(net, "nat");

	kfree(repl);
	return ret;
}

static void __net_exit ip6table_nat_net_pre_exit(struct net *net)
{
	ip6t_nat_unregister_lookups(net);
}

static void __net_exit ip6table_nat_net_exit(struct net *net)
{
	ip6t_unregister_table_exit(net, "nat");
}

static struct pernet_operations ip6table_nat_net_ops = {
	.pre_exit = ip6table_nat_net_pre_exit,
	.exit	= ip6table_nat_net_exit,
	.id	= &ip6table_nat_net_id,
	.size	= sizeof(struct ip6table_nat_pernet),
};

int __init droid_nat_shim_resolve(void);

static int __init ip6table_nat_init(void)
{
	int ret;

	ret = droid_nat_shim_resolve();
	if (ret)
		return ret;

	/* net->gen->ptr[ip6table_nat_net_id] must be allocated
	 * before calling ip6t_nat_register_lookups().
	 */
	ret = register_pernet_subsys(&ip6table_nat_net_ops);
	if (ret < 0)
		return ret;

	ret = xt_register_template(&nf_nat_ipv6_table,
				   ip6table_nat_table_init);
	if (ret)
		unregister_pernet_subsys(&ip6table_nat_net_ops);

	return ret;
}

static void __exit ip6table_nat_exit(void)
{
	xt_unregister_template(&nf_nat_ipv6_table);
	unregister_pernet_subsys(&ip6table_nat_net_ops);
}

module_init(ip6table_nat_init);
module_exit(ip6table_nat_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ip6tables legacy nat table");
